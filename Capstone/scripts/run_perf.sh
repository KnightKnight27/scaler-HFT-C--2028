#!/usr/bin/env bash
set -euo pipefail

# Capstone Linux perf Profiling Script
# Runs perf stat and perf record for Implementations A, B, and C as required by the Assessment Brief.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CAPSTONE_DIR="$(dirname "$SCRIPT_DIR")"
cd "$CAPSTONE_DIR"

echo "========================================================="
echo " Building Optimized Order Book Benchmark (-O2)          "
echo "========================================================="
make clean
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

OUTPUT_DIR="$CAPSTONE_DIR/perf_results"
mkdir -p "$OUTPUT_DIR"

PERF_EVENTS="task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"

echo ""
echo "========================================================="
echo " Running Correctness Verification                        "
echo "========================================================="
./bench --verify | tee "$OUTPUT_DIR/verify_output.txt"

for IMPL in A B C; do
    echo ""
    echo "========================================================="
    echo " [1/2] perf stat: Implementation $IMPL (5 runs)           "
    echo "========================================================="
    if command -v perf >/dev/null 2>&1; then
        perf stat -r 5 \
            -e "$PERF_EVENTS" \
            ./bench --impl "$IMPL" 2>&1 | tee "$OUTPUT_DIR/perf_stat_${IMPL}.txt"

        echo ""
        echo "========================================================="
        echo " [2/2] perf record: Implementation $IMPL (Call Graph)     "
        echo "========================================================="
        perf record -g -o "$OUTPUT_DIR/perf_${IMPL}.data" ./bench --impl "$IMPL"
        perf report -i "$OUTPUT_DIR/perf_${IMPL}.data" --stdio --no-children | head -n 40 > "$OUTPUT_DIR/perf_report_${IMPL}.txt"
        echo "Saved perf report to $OUTPUT_DIR/perf_report_${IMPL}.txt"
    else
        echo "WARNING: 'perf' command not found on this environment."
        echo "Running native standalone benchmark without perf..."
        ./bench --impl "$IMPL" --runs 5 | tee "$OUTPUT_DIR/bench_${IMPL}.txt"
    fi
done

echo ""
echo "========================================================="
echo " Benchmark & Profiling Complete! Logs saved to $OUTPUT_DIR"
echo "========================================================="
