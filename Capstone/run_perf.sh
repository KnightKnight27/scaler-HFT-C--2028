#!/bin/bash
# run this on linux (perf doesnt work on windows)
# it builds, runs the verify, then perf stat + perf record for A B C
# outputs go into perf_results/

g++ -std=c++17 -O2 -g -o bench bench.cpp || exit 1

mkdir -p perf_results

./bench --verify | tee perf_results/verify.txt

for impl in A B C
do
    echo "===== Implementation $impl ====="
    ./bench --impl $impl | tee perf_results/bench_$impl.txt

    perf stat -r 5 \
        -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
        ./bench --impl $impl > /dev/null 2> perf_results/stat_$impl.txt
    cat perf_results/stat_$impl.txt

    # perf.data goes in /tmp because perf record cant write to /mnt/c on WSL ("Bad address")
    perf record -g -o /tmp/perf_$impl.data ./bench --impl $impl > /dev/null
    perf report -i /tmp/perf_$impl.data --stdio --no-children 2>/dev/null | head -60 > perf_results/report_$impl.txt
done

echo "done, look in perf_results/"
