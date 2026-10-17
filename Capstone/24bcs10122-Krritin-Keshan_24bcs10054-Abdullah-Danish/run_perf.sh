#!/bin/bash
# runs inside the linux container (see README)
# perf stat + perf record + cachegrind for A, B, C and the workload-only baseline
set -e
cd "$(dirname "$0")"
mkdir -p results

# build in /tmp so the mac binary in this folder is not replaced
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer bench.cpp -o /tmp/bench

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

{
    echo "$(uname -srm), $(perf --version), $(g++ --version | head -1)"
    for impl in A B C none; do
        perf stat -r 5 -e $EVENTS /tmp/bench --impl $impl --perf 2>&1 > /dev/null
    done
} > results/perf_stat.txt

# inside docker perf only gets ~1000 samples per second, so each one runs 40 times
for impl in A B C; do
    perf record -g -o /tmp/perf_$impl.data -- sh -c "for i in \$(seq 40); do /tmp/bench --impl $impl --perf > /dev/null; done" 2> /dev/null
    echo "=============== Implementation $impl"
    perf report -i /tmp/perf_$impl.data --comm bench --stdio --no-children --percent-limit 2 2> /dev/null
done > results/perf_report.txt

# docker on a mac has no hardware counters, so cache and branch misses come from cachegrind
# (simulated caches set to the M3 Pro sizes: 192K L1i, 128K L1d, 16M L2, 128 byte lines)
for impl in A B C none; do
    echo "=============== $impl"
    valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
        --I1=196608,6,128 --D1=131072,8,128 --LL=16777216,16,128 \
        --cachegrind-out-file=/dev/null /tmp/bench --impl $impl --perf 2>&1 > /dev/null | sed 's/^==[0-9]*== //'
done > results/cachegrind.txt
