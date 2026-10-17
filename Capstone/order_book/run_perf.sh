#!/bin/bash
# runs perf stat and perf record for all three books
# this needs Linux (lab machine or WSL2 Ubuntu). perf does not exist on macOS
# usage: ./run_perf.sh

set -e
mkdir -p results

g++ -O2 -std=c++20 -Wall -g -o bench bench.cpp

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

for impl in A B C; do
    echo "perf stat for $impl"
    # perf stat already repeats 5 times with -r 5, so the program itself does only 1 run
    perf stat -r 5 -e $EVENTS ./bench --impl $impl --runs 1 > /dev/null 2> results/perf_stat_$impl.txt
    cat results/perf_stat_$impl.txt

    echo "perf record for $impl"
    perf record -g -o results/perf_$impl.data ./bench --impl $impl --runs 1 > /dev/null
    perf report -i results/perf_$impl.data --stdio --no-children 2> /dev/null | head -60 > results/perf_report_$impl.txt
done

echo "done, look inside results/"
