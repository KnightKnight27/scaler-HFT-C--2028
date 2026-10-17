#!/usr/bin/env bash
# Re-collect all perf data. Run from the project folder on a Linux machine.
set -e
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Iinclude src/bench.cpp -o bench
mkdir -p results
./bench --verify | tee results/verify.txt
for i in A B C; do
    ./bench --impl "$i" | tee "results/bench_$i.txt"
    perf stat -r 5 \
        -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
        ./bench --impl "$i" > /dev/null 2> "results/perf_stat_$i.txt"
    cat "results/perf_stat_$i.txt"
    perf record -g -o "perf_$i.data" ./bench --impl "$i" > /dev/null
    perf report -i "perf_$i.data" --stdio --no-children --sort symbol | head -60 > "results/perf_report_$i.txt"
done
