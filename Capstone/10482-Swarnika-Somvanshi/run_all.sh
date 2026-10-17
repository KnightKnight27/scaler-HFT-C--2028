#!/bin/bash
# run_all.sh
# Compiles, runs the tests, runs the benchmarks, and collects perf data.
# Everything is pinned to CPU core 2 so the OS doesn't move the program around.

CORE=2
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

mkdir -p results

echo "=== Compiling ==="
g++ -O2 -std=c++17 test.cpp -o test  || exit 1
g++ -O2 -std=c++17 bench.cpp -o bench || exit 1

echo
echo "=== Correctness test ==="
./test | tee results/test_output.txt

echo
echo "=== Benchmarks (pinned to core $CORE) ==="
for I in A B C; do
    taskset -c $CORE ./bench --impl $I | tee results/bench_$I.txt
    echo
done

echo "=== perf stat (5 runs each) ==="
for I in A B C; do
    echo "perf stat: implementation $I"
    taskset -c $CORE perf stat -r 5 -e $EVENTS \
        -o results/perf_stat_$I.txt ./bench --impl $I > /dev/null \
        || echo "  perf stat failed for $I (see the note below)"
done

echo
echo "=== perf record (call graphs, one pass each) ==="
for I in A B C; do
    echo "perf record: implementation $I"
    timeout 120 taskset -c $CORE perf record -F 999 -g -o results/perf_$I.data \
        ./bench --impl $I --runs 1 > /dev/null 2>&1 \
        || echo "  perf record failed for $I"
    perf report -i results/perf_$I.data --stdio --no-children -g none 2> /dev/null \
        | grep -v '^$' | head -40 > results/perf_report_$I.txt
done

echo
echo "Done. Everything is saved in the results/ folder."
