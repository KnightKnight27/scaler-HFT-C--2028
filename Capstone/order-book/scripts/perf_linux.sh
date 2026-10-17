#!/usr/bin/env bash
# Collects the Linux perf data used in REPORT.md. Run it on a Linux machine with
# perf installed:
#
#   scripts/perf_linux.sh [output-dir]        (default: results/linux)
#
# Set PERF=/path/to/perf if perf is not on the PATH. If valgrind is installed,
# the script also runs cachegrind, which simulates the caches and the branch
# predictor. That fills the gap on machines and VMs where perf cannot read the
# hardware counters.
set -euo pipefail

cd "$(dirname "$0")/.."
OUT=${1:-results/linux}
PERF=${PERF:-perf}
BIN=build-linux/bench
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

mkdir -p "$OUT" build-linux
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra src/*.cpp -o "$BIN"

{
    uname -a
    echo
    lscpu 2> /dev/null | grep -E "Model name|^CPU\(s\)|Thread|L1d|L2|L3" || true
    echo
    g++ --version | head -1
    "$PERF" --version
} > "$OUT/machine.txt"

echo "correctness test and benchmark..."
"$BIN" --verify > "$OUT/verify.txt"
"$BIN" --impl all > "$OUT/bench_all.txt"

# perf stat: 5 runs of one plain pass each. "none" only generates the workload,
# so subtracting it leaves the order book's share of every counter.
for impl in none A B C; do
    echo "perf stat --impl $impl..."
    "$PERF" stat -r 5 -e "$EVENTS" -o "$OUT/perf_stat_$impl.txt" \
        "$BIN" --impl "$impl" --perf > "$OUT/perf_stat_${impl}_program_output.txt"
done

# perf record: where the time goes. perf falls back to the cpu-clock software
# event when the hardware cycles event is not available. C's run is short, so
# sample at 10 kHz (the default is 4 kHz) to get enough samples.
for impl in A B C; do
    echo "perf record --impl $impl..."
    "$PERF" record -F 10000 -g -o "$OUT/perf_$impl.data" "$BIN" --impl "$impl" --perf > /dev/null 2> "$OUT/perf_record_$impl.log"
    "$PERF" report -i "$OUT/perf_$impl.data" --stdio --no-children -g none --percent-limit 0.5 \
        > "$OUT/perf_report_$impl.txt" 2> /dev/null
    "$PERF" report -i "$OUT/perf_$impl.data" --stdio --children --percent-limit 3 \
        > "$OUT/perf_report_callgraph_$impl.txt" 2> /dev/null
done

if command -v valgrind > /dev/null; then
    for impl in none A B C; do
        echo "cachegrind --impl $impl..."
        valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
            --cachegrind-out-file="$OUT/cachegrind.out.$impl" \
            "$BIN" --impl "$impl" --perf > /dev/null 2> "$OUT/cachegrind_$impl.txt"
        # Per-function totals only, without annotated copies of every source file.
        cg_annotate --no-annotate "$OUT/cachegrind.out.$impl" > "$OUT/cg_annotate_$impl.txt" 2> /dev/null || true
    done
fi

echo "results written to $OUT"
