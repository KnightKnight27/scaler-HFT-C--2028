#!/bin/bash
# Rebuilds everything and regenerates all the files in results/.
# Run it from anywhere:  bash scripts/run_all.sh
# perf needs:  sudo sysctl -w kernel.perf_event_paranoid=1

cd "$(dirname "$0")/.." || exit 1
mkdir -p results

# The laptop CPU has P-cores (0-3) and E-cores (4-11). Pin to one P-core so
# every run uses the same kind of core and perf reads one set of counters.
PIN="taskset -c 0"

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -o bench src/bench.cpp || exit 1

echo "== correctness"
./bench --verify | tee results/correctness.txt

echo "== perf stat baseline (workload generation only)"
$PIN perf stat -r 5 -e $EVENTS ./bench --gen-only > /dev/null 2> results/perf_stat_baseline.txt
cat results/perf_stat_baseline.txt

for impl in A B C; do
    echo "== benchmark $impl"
    $PIN ./bench --impl $impl | tee results/bench_$impl.txt

    echo "== perf stat $impl"
    $PIN perf stat -r 5 -e $EVENTS ./bench --impl $impl --runs 1 --total-only \
        > /dev/null 2> results/perf_stat_$impl.txt
    cat results/perf_stat_$impl.txt

    echo "== perf record $impl"
    $PIN perf record -g -o results/perf_$impl.data ./bench --impl $impl --runs 1 --total-only > /dev/null
    perf report -i results/perf_$impl.data --stdio --no-children --no-inline -g none --percent-limit 1 2> /dev/null \
        > results/perf_report_$impl.txt
done
