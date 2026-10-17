#!/bin/bash
# builds, verifies, benches and perfs all 3 impls. everything lands in results/
# arch: sudo pacman -S perf ; sudo sysctl kernel.perf_event_paranoid=1   (else perf complains)
# plug the charger in + close the browser first, battery mode throttles the cpu
mkdir -p results
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -o bench bench.cpp || exit 1

./bench --verify | tee results/verify.txt
for i in A B C; do ./bench --impl $i | tee results/bench_$i.txt; done

EV=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
for i in A B C; do
    echo "=== perf $i ==="
    perf stat -r 5 -e $EV ./bench --impl $i --perf 2>&1 >/dev/null | tee results/perf_stat_$i.txt
    perf record -g -o results/perf_$i.data ./bench --impl $i --perf >/dev/null
    perf report -i results/perf_$i.data --stdio --no-children 2>/dev/null | head -80 > results/perf_report_$i.txt
done
echo "done, look in results/"
