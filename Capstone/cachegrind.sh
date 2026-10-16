#!/usr/bin/env bash
# Rohan Ranjan-10428-Capstone project
#
# branch + cache misses with valgrind cachegrind (needs Linux + valgrind)
#   ./cachegrind.sh                -> writes into results/cachegrind
#
# perf can't read the hardware counters on my Mac (Docker VM / macOS), so this runs
# the program on valgrind's simulated CPU instead. the numbers are SIMULATED:
# good for comparing A / B / C, not exact hardware counts.
# the simulated caches are set to the Apple M4 performance core sizes:
#   L1 instr 192 KB 6-way, L1 data 128 KB 8-way, last level 16 MB 16-way, 128-byte lines
# "none" only builds the workload, subtract it to get the order book part.

set -e
cd "$(dirname "$0")"
OUT=${1:-results/cachegrind}
mkdir -p "$OUT"

BIN=$(mktemp -d)/bench
g++ -std=c++20 -O2 -g bench.cpp -o "$BIN"
valgrind --version > "$OUT/env.txt"

for impl in none A B C; do
    echo "cachegrind: $impl"
    valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
        --I1=196608,6,128 --D1=131072,8,128 --LL=16777216,16,128 \
        --cachegrind-out-file=/dev/null \
        "$BIN" --impl "$impl" --total-only --runs 1 > /dev/null 2> "$OUT/cachegrind_$impl.txt"
done

# book only = impl - none
awk '
    FNR == 1 { split(FILENAME, p, "_"); name = p[length(p)]; sub(/\.txt$/, "", name) }
    { gsub(",", "") }
    /I *refs:/       { ir[name] = $4 }
    /D *refs:/       { dr[name] = $4 }
    /D1 *misses:/    { d1[name] = $4 }
    /LLd *misses:/   { ll[name] = $4 }
    /Branches:/      { br[name] = $3 }
    /Mispredicts:/   { mp[name] = $3 }
    END {
        printf "order book only (impl minus none), simulated by cachegrind\n\n"
        printf "%-5s %14s %14s %12s %12s %12s %12s %9s\n", "impl", "instr", "data refs", "D1 misses", "LLd misses", "branches", "mispredicts", "mispred%"
        n = split("A B C", list, " ")
        for (k = 1; k <= n; k++) {
            x = list[k]
            b = br[x] - br["none"]; m = mp[x] - mp["none"]
            printf "%-5s %14d %14d %12d %12d %12d %12d %8.2f%%\n", x, ir[x] - ir["none"], dr[x] - dr["none"], \
                d1[x] - d1["none"], ll[x] - ll["none"], b, m, 100 * m / b
        }
    }' "$OUT"/cachegrind_none.txt "$OUT"/cachegrind_A.txt "$OUT"/cachegrind_B.txt "$OUT"/cachegrind_C.txt | tee "$OUT/summary.txt"
