#!/usr/bin/env bash
# Rohan Ranjan-10428-Capstone project
#
# macOS has no perf, but /usr/bin/time -l still prints cycles, instructions,
# page faults and peak memory for the whole process. this script uses that.
#
# "none" only builds the workload, so I subtract its numbers to get
# just the order book part. every number is the median of 5 tries.
#   ./mac_counters.sh   -> prints a table (also saved in results/mac_counters.txt)

set -e
cd "$(dirname "$0")"
mkdir -p results
BIN=$(mktemp -d)/bench
g++ -std=c++20 -O2 bench.cpp -o "$BIN"

RUNS=20   # order book runs per process for the IPC numbers (so the book is most of the work)
TRIES=5

# prints: instructions cycles page_faults peak_memory
measure() {
    /usr/bin/time -l "$BIN" --impl "$1" --total-only --runs "$2" 2>&1 >/dev/null | awk '
        /instructions retired/  {i = $1}
        /cycles elapsed/        {c = $1}
        /page reclaims/         {r = $1}
        / page faults/          {f = $1}
        /peak memory footprint/ {m = $1}
        END {print i, c, r + f, m}'
}

# median of column $1 from stdin
med() { sort -n -k"$1" | awk -v k="$1" '{a[NR] = $k} END {print a[int((NR + 1) / 2)]}'; }

# one line per impl: name instructions cycles page_faults peak_memory
ROWS=""
for impl in none A B C; do
    big=$(for t in $(seq $TRIES); do measure "$impl" "$RUNS"; done)
    one=$(for t in $(seq $TRIES); do measure "$impl" 1; done)
    ROWS="$ROWS$impl $(echo "$big" | med 1) $(echo "$big" | med 2) $(echo "$one" | med 3) $(echo "$one" | med 4)
"
done

{
    echo "macOS counters ($(sysctl -n machdep.cpu.brand_string)), median of $TRIES tries"
    echo "IPC uses $RUNS runs per process; page faults and memory use 1 run. 'none' is subtracted."
    echo
    printf "%-5s %16s %16s %8s %14s %14s\n" impl "instr/run" "cycles/run" IPC "page faults" "peak mem MB"
    printf "%s" "$ROWS" | awk -v r="$RUNS" '
        $1 == "none" {i0 = $2; c0 = $3; p0 = $4; m0 = $5; next}
        {printf "%-5s %16.0f %16.0f %8.2f %14d %14.1f\n", $1, ($2-i0)/r, ($3-c0)/r, ($2-i0)/($3-c0), $4-p0, $5/1048576}
        END {printf "\nraw none (workload only): instructions %d, cycles %d, page faults %d, peak mem %.1f MB\n", i0, c0, p0, m0/1048576}'
} | tee results/mac_counters.txt
