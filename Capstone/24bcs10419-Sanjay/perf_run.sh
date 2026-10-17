#!/bin/bash
set -e

BENCH="./bench"
OUTDIR="perf_results"
mkdir -p "$OUTDIR"

COUNTERS="task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"

for IMPL in A B C; do
    echo "Running perf stat for Implementation ${IMPL}..."
    perf stat -r 5 -e "$COUNTERS" "$BENCH" --impl "$IMPL" 2>&1 | tee "$OUTDIR/perf_stat_${IMPL}.txt"

    echo "Running perf record for Implementation ${IMPL}..."
    perf record -g -o "$OUTDIR/perf_${IMPL}.data" "$BENCH" --impl "$IMPL"
done
