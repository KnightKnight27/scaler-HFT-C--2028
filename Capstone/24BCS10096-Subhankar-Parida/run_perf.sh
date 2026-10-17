#!/bin/bash
# run_perf.sh — Runs perf stat and perf record for all three implementations + baseline.
# Meant to be run inside the Docker container:
#   docker build -t ob-perf -f perf.Dockerfile .
#   docker run --rm --privileged -v "$PWD":/work -w /work ob-perf ./run_perf.sh

set -e

echo "=== Building ==="
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -o /tmp/bench bench.cpp

echo ""
echo "=== perf stat: baseline (workload only) ==="
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  /tmp/bench --impl none --perf 2>&1

for IMPL in A B C; do
    echo ""
    echo "=== perf stat: Implementation $IMPL ==="
    perf stat -r 5 \
      -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
      /tmp/bench --impl $IMPL --perf 2>&1
done

echo ""
echo "=== perf record ==="
for IMPL in A B C; do
    echo ""
    echo "--- perf record: Implementation $IMPL ---"
    # run 10 times under perf record for more samples
    for i in $(seq 1 10); do
        perf record -q -g -o /tmp/perf_${IMPL}.data -- /tmp/bench --impl $IMPL --perf > /dev/null 2>&1 || true
    done
    perf report --no-children -i /tmp/perf_${IMPL}.data 2>/dev/null | head -30
done

echo ""
echo "=== cachegrind ==="
for IMPL in none A B C; do
    echo ""
    echo "--- cachegrind: Implementation $IMPL ---"
    valgrind --tool=cachegrind --cachegrind-out-file=/tmp/cg_${IMPL}.out /tmp/bench --impl $IMPL --perf 2>&1 | tail -20
done

echo ""
echo "Done."
