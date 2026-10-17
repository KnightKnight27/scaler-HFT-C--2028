#!/usr/bin/env bash
# Builds the project, runs the correctness test and the benchmarks, and saves
# the output. Works on macOS and Linux:
#
#   scripts/run_all.sh [output-dir]           (default: results/<os name>)
set -euo pipefail

cd "$(dirname "$0")/.."
OUT=${1:-results/$(uname -s | tr '[:upper:]' '[:lower:]')}

cmake -S . -B build > /dev/null
cmake --build build > /dev/null
mkdir -p "$OUT"

./build/bench --verify | tee "$OUT/verify.txt"
./build/bench --impl all | tee "$OUT/bench_all.txt"

# One process per implementation as well, so that each one's peak memory is
# reported on its own.
for impl in A B C; do
    ./build/bench --impl "$impl" > "$OUT/bench_$impl.txt"
done

# Peak memory of one plain pass. "none" only generates the workload, so the
# difference is the memory used by the order book.
for impl in none A B C; do
    echo "$impl: $(./build/bench --impl "$impl" --perf | grep 'Peak memory')"
done > "$OUT/memory.txt"

echo "results written to $OUT"
