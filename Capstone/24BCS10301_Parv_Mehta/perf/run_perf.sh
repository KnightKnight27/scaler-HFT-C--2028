#!/usr/bin/env bash
# Profiling pass for all three implementations: perf stat, perf record/report
# and (optionally) cachegrind. Needs Linux. On a Mac, perf/docker_perf.sh runs
# this script inside the container built from perf/Dockerfile.
#
#   OPS=1000000 CACHEGRIND=1 perf/run_perf.sh
#
# Results land in perf/results/.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=perf/results
BIN=build-linux
OPS=${OPS:-1000000}
CACHEGRIND=${CACHEGRIND:-1}
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

mkdir -p "$OUT"
# frame pointers so that perf record -g can walk the stack without DWARF
make BINDIR=$BIN CXXFLAGS_EXTRA=-fno-omit-frame-pointer $BIN/bench

{
    echo "date:     $(date -u)"
    echo "kernel:   $(uname -a)"
    echo "compiler: $(g++ --version | head -1)"
    echo "perf:     $(perf --version)"
    echo "valgrind: $(valgrind --version 2>/dev/null || echo 'not installed')"
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid)"
    echo
    lscpu 2>/dev/null || head -30 /proc/cpuinfo
} > "$OUT/machine_info.txt"

# "none" is the empty baseline: the same harness and workload generation with
# no order book behind it. Subtract it from A/B/C to get the book's own cost.
for X in none A B C; do
    echo "=== perf stat, implementation $X ==="
    perf stat -r 5 -e "$EVENTS" -- "$BIN/bench" --impl "$X" --timing total --ops "$OPS" 2>&1 | tee "$OUT/perf_stat_$X.txt"
done

for X in A B C; do
    echo "=== perf record, implementation $X ==="
    # cpu-clock is a software event; it is what perf falls back to when the
    # hardware cycles counter is unavailable (e.g. inside a VM).
    # --repeat 5 runs the workload 5 times in one process so the order book,
    # not the one-off workload generation, dominates the samples.
    perf record -e cpu-clock -F 10000 -g -o "$OUT/perf_$X.data" -- "$BIN/bench" --impl "$X" --timing total --repeat 5 --ops "$OPS" > /dev/null 2> "$OUT/perf_record_$X.log"
    perf report -i "$OUT/perf_$X.data" --stdio --no-children -g none --percent-limit 0.3 > "$OUT/perf_report_$X.txt" 2>&1
done

if [ "$CACHEGRIND" = 1 ] && command -v valgrind > /dev/null; then
    # Cache parameters roughly matching the Apple M-series performance core
    # this was developed on (cachegrind cannot auto-detect them on arm64):
    # 128 KB 8-way L1d, 192 KB 6-way L1i, 16 MB 16-way last level, 64 B lines.
    CG="--I1=196608,6,64 --D1=131072,8,64 --LL=16777216,16,64"
    for X in A B C; do
        echo "=== cachegrind, implementation $X ==="
        valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes $CG \
            --cachegrind-out-file="$OUT/cachegrind_$X.out" \
            "$BIN/bench" --impl "$X" --timing total --ops "$OPS" 2>&1 | tee "$OUT/cachegrind_$X.txt"
    done
fi

echo "done, results in $OUT/"
