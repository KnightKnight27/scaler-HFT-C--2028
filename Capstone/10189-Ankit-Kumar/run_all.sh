#!/usr/bin/env bash
# Builds, verifies, benchmarks and profiles the three order books. Output goes to results/.
#
#   ./run_all.sh                  # uses `perf` from PATH
#   PERF=/path/to/perf ./run_all.sh
#
# Needs: g++, perf. Optional: valgrind (simulated cache/branch counts when the CPU's
# hardware counters are not available, e.g. inside WSL2).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p results

PERF=${PERF:-perf}
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
PIN=""
command -v taskset >/dev/null && PIN="taskset -c 2"   # keep the benchmark on one CPU

# drop blank lines, perf's tip line and the empty IPC columns; cut very long C++ symbol names
tidy_report() { grep -v -e '^$' -e 'tips.txt' | sed -E 's/ +-( +-)? *$//; s/ +$//' | cut -c1-160; }

echo "== build"
g++ -std=c++17 -O2 -g -Wall -Wextra -o bench bench.cpp
# same program, but book C uses Fibonacci hashing for its order table (hash experiment)
g++ -std=c++17 -O2 -g -DORDER_TABLE_FIBONACCI_HASH -o bench_fib bench.cpp

{
    echo "kernel:   $(uname -srm)"
    lscpu | grep -E '^(Model name|CPU\(s\)|Thread|L1d|L2|L3|Hypervisor)' | sed 's/  */ /g'
    echo "compiler: $(g++ --version | head -1)"
    echo "flags:    -std=c++17 -O2 -g"
    echo "perf:     $($PERF --version)"
    command -v valgrind >/dev/null && echo "valgrind: $(valgrind --version)"
    echo "pinning:  ${PIN:-none}"
} > results/system.txt
cat results/system.txt

echo "== correctness"
./bench --verify | tee results/verify.txt

for impl in A B C; do
    echo "== benchmark $impl"
    $PIN ./bench --impl $impl | tee results/bench_$impl.txt

    echo "== perf stat $impl"
    # --runs 1 so that `perf stat -r 5` is exactly 5 runs; --no-latency so the counters
    # only see order-book work, not 2M clock reads
    $PIN $PERF stat -r 5 -e $EVENTS ./bench --impl $impl --runs 1 --no-latency 2> results/perf_stat_$impl.txt >/dev/null
    cat results/perf_stat_$impl.txt

    echo "== perf record $impl"
    $PIN $PERF record -g -F 10000 -o /tmp/perf_$impl.data ./bench --impl $impl --runs 5 --no-latency >/dev/null 2>/tmp/perf_record_$impl.log
    $PERF report -i /tmp/perf_$impl.data --stdio --no-children --sort symbol --percent-limit 1 -g none \
        2>/dev/null | tidy_report > results/perf_report_$impl.txt
    # most of the hot code is inlined STL internals, so also split the profile by source line
    $PERF report -i /tmp/perf_$impl.data --stdio --no-children --sort srcline --percent-limit 1.5 -g none \
        2>/dev/null | tidy_report > results/perf_report_srcline_$impl.txt
    head -30 results/perf_report_$impl.txt

    if command -v valgrind >/dev/null; then
        echo "== cachegrind $impl"
        valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --cachegrind-out-file=/dev/null \
            ./bench --impl $impl --runs 1 --no-latency 2>&1 >/dev/null | grep -E '^==[0-9]+== (Command|I |I1|LLi|D |D1|LLd|LL |Branches|Mispred)' > results/cachegrind_$impl.txt
        cat results/cachegrind_$impl.txt
    fi
done

echo "== baseline (workload generation only, the fixed cost inside every run)"
$PIN $PERF stat -r 5 -e $EVENTS ./bench --baseline 2> results/perf_stat_baseline.txt >/dev/null
cat results/perf_stat_baseline.txt
if command -v valgrind >/dev/null; then
    valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --cachegrind-out-file=/dev/null \
        ./bench --baseline 2>&1 >/dev/null | grep -E '^==[0-9]+== (Command|I |I1|LLi|D |D1|LLd|LL |Branches|Mispred)' > results/cachegrind_baseline.txt
fi

echo "== hash experiment: book C with Fibonacci hashing"
$PIN ./bench_fib --impl C | tee results/experiment_C_fibonacci_hash.txt
if command -v valgrind >/dev/null; then
    valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --cachegrind-out-file=/dev/null \
        ./bench_fib --impl C --runs 1 --no-latency 2>&1 >/dev/null | grep -E '^==[0-9]+== (Command|I |I1|LLi|D |D1|LLd|LL |Branches|Mispred)' \
        > results/cachegrind_C_fibonacci_hash.txt
    cat results/cachegrind_C_fibonacci_hash.txt
fi

echo "== id experiment: random-looking 64-bit ids instead of 1, 2, 3, ..."
for impl in A B C; do
    $PIN ./bench --impl $impl --ids scrambled | tee results/experiment_${impl}_scrambled_ids.txt
done
