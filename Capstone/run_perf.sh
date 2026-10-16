#!/usr/bin/env bash
# Rohan Ranjan-10428-Capstone project
#
# perf stat + perf record for A, B and C (needs Linux + perf)
#   ./run_perf.sh                 -> writes into results/linux
#   ./run_perf.sh some/folder     -> writes there
#
# on a Mac there is no perf, so run it inside Docker (see README).
# "none" only builds the workload, so its numbers show the cost that is NOT the order book.

set -e
cd "$(dirname "$0")"
OUT=${1:-results/linux}
mkdir -p "$OUT"

BIN=$(mktemp -d)/bench
g++ -std=c++20 -O2 -g -fno-omit-frame-pointer bench.cpp -o "$BIN"

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

for impl in none A B C; do
    echo "perf stat: $impl"
    perf stat -r 5 -e "$EVENTS" "$BIN" --impl "$impl" --total-only --runs 1 > /dev/null 2> "$OUT/perf_stat_$impl.txt"
done

for impl in A B C; do
    echo "perf record: $impl"
    perf record -g -o "/tmp/perf_$impl.data" "$BIN" --impl "$impl" --total-only --runs 10 > /dev/null 2> "$OUT/perf_record_$impl.log"
    perf report -i "/tmp/perf_$impl.data" --stdio --no-children -g none --percent-limit 1 2> /dev/null \
        | grep -v '^$' > "$OUT/perf_report_$impl.txt"
done

echo "done, results in $OUT"
