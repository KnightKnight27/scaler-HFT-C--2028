#!/usr/bin/env bash
#
# Runs the perf measurements required by section 8 of the capstone
# assignment, for all three OrderBook implementations:
#   - perf stat -r 5 -e <9 counters>   (section 8, "perf stat")
#   - perf record -g / perf report     (section 8, "perf record")
#   - ./bench --verify                 (section 5, correctness proof)
#
# perf is Linux-only. Run this on a real Linux machine (a Linux VM/container
# works too, as long as it has real hardware PMU access -- Docker Desktop on
# Apple Silicon does NOT, see the README for why).
#
# Usage:
#   ./run_perf.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "warning: this does not look like Linux (uname -s = $(uname -s))." >&2
    echo "perf requires Linux -- continuing anyway in case this is a container." >&2
fi

if ! command -v perf >/dev/null 2>&1; then
    echo "error: 'perf' not found on PATH. On Debian/Ubuntu, install it with:" >&2
    echo "  sudo apt-get install linux-tools-common linux-tools-generic linux-tools-\$(uname -r)" >&2
    exit 1
fi

echo "Building bench (g++ -O2)..."
make bench

OUT_DIR="perf_results"
mkdir -p "$OUT_DIR"

EVENTS="task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"

for impl in A B C; do
    echo ""
    echo "=== perf stat: Implementation $impl ==="
    if perf stat -r 5 -e "$EVENTS" ./bench --impl "$impl" \
        > "$OUT_DIR/stat_${impl}.bench_output.txt" \
        2> "$OUT_DIR/stat_${impl}.txt"; then
        cat "$OUT_DIR/stat_${impl}.txt"
    else
        echo "perf stat exited non-zero for $impl -- check $OUT_DIR/stat_${impl}.txt" >&2
        echo "(this can still be fine if only some counters are unavailable;" >&2
        echo " perf stat reports those as <not supported> and keeps going)" >&2
        cat "$OUT_DIR/stat_${impl}.txt" || true
    fi

    echo ""
    echo "=== perf record: Implementation $impl ==="
    if perf record -g -o "$OUT_DIR/record_${impl}.data" -- ./bench --impl "$impl" \
        > "$OUT_DIR/record_${impl}.bench_output.txt" 2>&1; then
        perf report -i "$OUT_DIR/record_${impl}.data" --stdio --no-children \
            > "$OUT_DIR/report_${impl}.txt" 2>/dev/null \
            || perf report -i "$OUT_DIR/record_${impl}.data" --stdio \
                > "$OUT_DIR/report_${impl}.txt"
        echo "saved $OUT_DIR/report_${impl}.txt"
    else
        echo "perf record failed for $impl -- check $OUT_DIR/record_${impl}.bench_output.txt" >&2
        echo "(common cause: perf_event_paranoid is too restrictive -- try:" >&2
        echo "   sudo sysctl -w kernel.perf_event_paranoid=1" >&2
        echo " or run this script with sudo)" >&2
    fi
done

echo ""
echo "=== correctness check ==="
./bench --verify | tee "$OUT_DIR/verify.txt"

echo ""
echo "All done. Results are in $OUT_DIR/"
echo "  stat_<impl>.txt    perf stat output -- the numbers for the results table"
echo "  report_<impl>.txt  perf report -- where time is spent, for the analysis"
echo "  verify.txt         correctness proof"
