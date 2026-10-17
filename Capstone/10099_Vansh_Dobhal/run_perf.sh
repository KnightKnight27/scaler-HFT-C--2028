#!/bin/bash
# builds the benchmark and saves verify, bench, perf stat and perf report output into results/
# pinned to cpu 0 (a performance core on my laptop) so all counters come from the same core type

set -e
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -o bench bench.cpp
mkdir -p results

./bench --verify | tee results/verify.txt

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

# baseline: only generating the workload, no order book
taskset -c 0 perf stat -r 5 -e $EVENTS ./bench --impl none 2> results/perf_stat_baseline.txt > /dev/null

for impl in A B C; do
    taskset -c 0 ./bench --impl $impl | tee results/bench_$impl.txt

    taskset -c 0 perf stat -r 5 -e $EVENTS ./bench --impl $impl --runs 1 2> results/perf_stat_$impl.txt > /dev/null
    cat results/perf_stat_$impl.txt

    taskset -c 0 perf record -g -o perf_$impl.data ./bench --impl $impl --runs 1 > /dev/null 2>&1
    perf report -i perf_$impl.data --stdio --no-children --comm bench --sort symbol -g none --percent-limit 1 2>/dev/null | cut -c1-160 > results/perf_report_$impl.txt
done
