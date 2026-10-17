#!/bin/bash
# runs verify, bench and perf for all 3 impls, saves output in results/
set -e
g++ -std=c++17 -O2 -g -o bench bench.cpp
mkdir -p results

./bench --verify | tee results/verify.txt

for impl in A B C; do
    ./bench --impl $impl | tee results/bench_$impl.txt
    perf stat -r 5 \
        -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
        ./bench --impl $impl > /dev/null 2> results/perf_stat_$impl.txt
    cat results/perf_stat_$impl.txt
    perf record -g -o perf_$impl.data ./bench --impl $impl > /dev/null 2>&1
    perf report -i perf_$impl.data --stdio --no-children -g none --percent-limit 1 2>/dev/null | grep -v "^$" > results/perf_report_$impl.txt
done
