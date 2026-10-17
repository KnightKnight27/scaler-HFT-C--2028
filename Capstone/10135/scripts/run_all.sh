#!/usr/bin/env bash
# Build, verify, benchmark and profile all three order books on Linux.
#
# Run this from the project root (the folder that holds the Makefile):
#   bash scripts/run_all.sh
#
# It writes everything into results/. perf counters that the machine does not
# expose are reported by perf itself as "<not supported>"; that is fine, just
# note which ones in the report.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p results

echo "== building (-O2) =="
make clean >/dev/null 2>&1 || true
make || { echo "build failed"; exit 1; }

echo "== correctness =="
./bench --verify | tee results/correctness.txt

echo "== benchmark =="
for impl in A B C; do
    echo "-- impl $impl --"
    ./bench --impl "$impl" | tee "results/benchmark-$impl.txt"
done

if ! command -v perf >/dev/null 2>&1; then
    echo "perf not found; skipping profiling. Install linux-tools and re-run for perf data."
    exit 0
fi

EVENTS="task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"

echo "== perf stat (-r 5) =="
for impl in A B C; do
    echo "-- perf stat $impl --"
    perf stat -r 5 -e "$EVENTS" ./bench --impl "$impl" > "results/perf-stat-$impl.txt" 2>&1
    cat "results/perf-stat-$impl.txt"
done

echo "== perf record / report =="
for impl in A B C; do
    echo "-- perf record $impl --"
    perf record -g -o "results/perf-$impl.data" ./bench --impl "$impl" >/dev/null 2>&1
    perf report -i "results/perf-$impl.data" --stdio 2>/dev/null | head -n 40 > "results/perf-report-$impl.txt"
    cat "results/perf-report-$impl.txt"
done

rm -f results/perf-*.data
echo "== done; results are in results/ =="
