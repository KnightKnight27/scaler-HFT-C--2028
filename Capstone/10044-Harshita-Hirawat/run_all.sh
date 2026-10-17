#!/bin/bash
# builds, verifies, benchmarks and profiles the 3 order books (A, B, C)
# output goes into results/
set -e
mkdir -p results

g++ -O2 -std=c++17 bench.cpp -o bench
g++ -O2 -std=c++17 test.cpp -o test

./test | tee results/verify.txt

for impl in A B C; do
  ./bench --impl $impl | tee results/bench_$impl.txt

  # --runs 1 so that perf -r 5 does exactly 5 runs
  perf stat -r 5 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
    ./bench --impl $impl --runs 1 > /dev/null 2> results/perf_stat_$impl.txt
  cat results/perf_stat_$impl.txt

  perf record -g -o results/perf_$impl.data ./bench --impl $impl --runs 1 > /dev/null
  DEBUGINFOD_URLS= perf report -i results/perf_$impl.data --stdio --no-children -g none --percent-limit 2 \
    < /dev/null > results/perf_report_$impl.txt 2>&1
done
