#!/usr/bin/env bash
# builds everything, runs --verify, the benchmark and perf for A, B, C
# and saves all the output in results/
set -e
cd "$(dirname "$0")"

# check perf works before anything in results/ gets overwritten
if ! perf stat -e task-clock true > /dev/null 2>&1; then
    echo "perf doesn't work here (not installed, or not allowed)."
    echo "try sudo, or lower /proc/sys/kernel/perf_event_paranoid (we used docker --privileged)"
    exit 1
fi

make -s bench
mkdir -p results

{ lscpu | grep -E "Model name|^CPU\(s\)|Thread|cache"; uname -r; g++ --version | head -1; perf --version; } > results/machine.txt

./bench --verify | tee results/verify.txt

for impl in A B C; do
    ./bench --impl $impl | tee results/bench_$impl.txt
done

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

for impl in A B C; do
    echo "perf stat $impl"
    perf stat -r 5 -e $EVENTS ./bench --impl $impl > /dev/null 2> results/perf_stat_$impl.txt
    cat results/perf_stat_$impl.txt

    # same thing without the per-op timing pass, so clock_gettime doesn't
    # dominate the counters
    perf stat -r 5 -e $EVENTS ./bench --impl $impl --no-timers > /dev/null 2> results/perf_stat_${impl}_no_timers.txt
    cat results/perf_stat_${impl}_no_timers.txt

    echo "perf record $impl"
    perf record -g -o perf.data ./bench --impl $impl > /dev/null 2>&1
    {
        echo "### self time (perf report --no-children)"
        perf report -i perf.data --stdio --no-children -g none --percent-limit 0.5 2>/dev/null | grep -v "^$"
        echo
        echo "### including children, with call graph (perf report --children)"
        perf report -i perf.data --stdio --children -g caller,0.5,callee --percent-limit 5 2>/dev/null | grep -v "^$" | head -150
    } > results/perf_report_$impl.txt
done
rm -f perf.data perf.data.old

echo "per run counters"
./per_run_counters.sh | tee results/perf_per_run.txt

# page faults (REPORT 11.4): --runs 1 vs --runs 5, and the same with glibc told
# to keep freed memory in the heap instead of giving big blocks back to the kernel
echo "page faults"
faults() { perf stat -x, -r 5 -e page-faults "$@" 2>&1 > /dev/null | cut -d, -f1; }
{
    echo "### page faults (perf stat -r 5 average), --runs 1 vs --runs 5"
    for impl in A B C; do
        echo "$impl  --runs 1: $(faults ./bench --impl $impl --runs 1)   --runs 5: $(faults ./bench --impl $impl --runs 5)"
    done
    echo
    echo "### --runs 5 again with MALLOC_MMAP_THRESHOLD_=67108864 MALLOC_TRIM_THRESHOLD_=1073741824"
    for impl in A B C; do
        echo "$impl  $(export MALLOC_MMAP_THRESHOLD_=67108864 MALLOC_TRIM_THRESHOLD_=1073741824; faults ./bench --impl $impl --runs 5)"
    done
} | tee results/page_faults.txt

# experiment from the report: C with its order table pre-sized to 2^21 slots
# (32 MB) instead of starting at 2^16 and doubling
echo "pre-size experiment"
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -DORDER_TABLE_INIT_CAP=2097152 -o bench_presized bench.cpp
{
    echo "### C as submitted (table starts at 2^16 slots and doubles, ends at 2^20 = 16 MB)"
    ./bench --impl C | sed -n '/median/,/bbo/p'
    perf stat -r 5 -e task-clock,page-faults ./bench --impl C 2>&1 > /dev/null | grep -E "task-clock|page-faults"
    echo
    echo "### C built with -DORDER_TABLE_INIT_CAP=2097152 (2^21 slots = 32 MB from the start)"
    ./bench_presized --impl C | sed -n '/median/,/bbo/p'
    perf stat -r 5 -e task-clock,page-faults ./bench_presized --impl C 2>&1 > /dev/null | grep -E "task-clock|page-faults"
} | tee results/experiment_presize.txt
rm -f bench_presized
