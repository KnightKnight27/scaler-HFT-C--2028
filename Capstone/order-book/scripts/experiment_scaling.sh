#!/usr/bin/env bash
# Cost per operation as the book grows. For workloads of 100k to 3M operations,
# runs one plain pass (--perf) and divides its total time by the number of
# operations. Sizes and implementations are interleaved and the whole set is
# repeated ROUNDS times; the median is reported.
#
#   scripts/experiment_scaling.sh            (uses build/bench)
set -euo pipefail

cd "$(dirname "$0")/.."
BIN=${BIN:-build/bench}
ROUNDS=${ROUNDS:-5}
SIZES="100000 300000 1000000 3000000"

raw=$(mktemp)
trap 'rm -f "$raw"' EXIT

for round in $(seq "$ROUNDS"); do
    for ops in $SIZES; do
        for impl in A B C; do
            ms=$("$BIN" --impl "$impl" --perf --ops "$ops" | awk '/Total time/ {print $3}')
            echo "$ops $impl $ms" >> "$raw"
        done
    done
done

echo "ns per operation (total time of one pass / operations), median of $ROUNDS rounds"
printf "%10s %10s %10s %10s\n" ops A B C
for ops in $SIZES; do
    printf "%10s" "$ops"
    for impl in A B C; do
        median_ms=$(awk -v o="$ops" -v i="$impl" '$1 == o && $2 == i {print $3}' "$raw" | sort -n |
            awk '{v[NR] = $1} END {print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2}')
        awk -v ms="$median_ms" -v o="$ops" 'BEGIN {printf " %10.1f", ms * 1e6 / o}'
    done
    echo
done
