#!/bin/bash

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

mkdir -p results
g++ -O2 -g -fno-omit-frame-pointer -std=c++17 -o bench bench.cpp || exit 1

./bench --verify | tee results/verify.txt

for impl in A B C; do
    echo "===== Implementation $impl ====="
    ./bench --impl $impl | tee results/bench_$impl.txt

    perf stat -r 5 -e $EVENTS ./bench --impl $impl --runs 1 > /dev/null 2> results/perf_stat_$impl.txt
    cat results/perf_stat_$impl.txt

    perf record -g -o /tmp/perf_$impl.data ./bench --impl $impl --runs 1 > /dev/null
    perf report -i /tmp/perf_$impl.data --stdio --no-children -g none --percent-limit 1 2> /dev/null | grep -v "^$" > results/perf_report_$impl.txt
done
