#!/usr/bin/env bash
# Collects all perf data for the report. Run from anywhere on a LINUX machine with `perf` + g++:
#     ./scripts/run_perf.sh
# Output goes to results/. Counters the machine does not support show up as <not supported>.
set -u
cd "$(dirname "$0")/.."
BUILD_DIR="${BUILD_DIR:-build}"   # perf needs to read symbols from the binary: use a local disk dir
mkdir -p "$BUILD_DIR" results

g++ -O2 -std=c++17 bench.cpp -o "$BUILD_DIR/bench" || exit 1
# separate build only for `perf record`: same -O2, plus symbols and frame pointers for call graphs
g++ -O2 -g -fno-omit-frame-pointer -std=c++17 bench.cpp -o "$BUILD_DIR/bench_prof" || exit 1

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

{ uname -srm; (lscpu 2>/dev/null | grep -E 'Model name|Architecture|^CPU\(s\)') || true; nproc; perf --version; cat /proc/sys/kernel/perf_event_paranoid; } > results/environment.txt 2>&1

# --reps 10: the order-book loop runs 10x per process so it dominates the (identical) workload
# generation at start-up. Baseline = generation only, so its share can be subtracted.
echo "perf stat: generation-only baseline"
perf stat -r 5 -e "$EVENTS" "$BUILD_DIR/bench" --impl none --perf > /dev/null 2> results/perf_stat_baseline_generation_only.txt
for I in A B C; do
    echo "perf stat: implementation $I"
    perf stat -r 5 -e "$EVENTS" "$BUILD_DIR/bench" --impl "$I" --perf --reps 10 > /dev/null 2> "results/perf_stat_$I.txt"
done

for I in A B C; do
    echo "perf record: implementation $I"
    perf record -g -o "results/perf_$I.data" "$BUILD_DIR/bench_prof" --impl "$I" --perf --reps 10 > /dev/null 2> "results/perf_record_$I.log"
    perf report -i "results/perf_$I.data" --stdio --no-children 2> /dev/null | head -80 > "results/perf_report_$I.txt"
    rm -f "results/perf_$I.data"
done
echo "done -> results/"
