#!/usr/bin/env bash
# macOS: build, verify, benchmark A/B/C and collect the counters macOS exposes
# without root (/usr/bin/time -l: instructions retired, cycles, page faults, RSS).
# Output goes to results/macos/.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=results/macos
mkdir -p "$OUT"

g++ -std=c++17 -O2 -Wall -Wextra -o bench src/bench.cpp
./bench --verify | tee "$OUT/verify.txt"
sysctl -n machdep.cpu.brand_string > "$OUT/env.txt"
g++ --version | head -1 >> "$OUT/env.txt"
sw_vers >> "$OUT/env.txt"

for impl in A B C 0; do
  ./bench --impl "$impl" | tee "$OUT/bench_$impl.txt"
  [ "$impl" != 0 ] && ./bench --impl "$impl" --untimed > "$OUT/bench_${impl}_untimed.txt"
  # Whole-process counters; --untimed so the per-op clock reads are not counted.
  : > "$OUT/time_$impl.txt"
  for i in 1 2 3 4 5; do
    /usr/bin/time -l ./bench --impl "$impl" --untimed > /dev/null 2>> "$OUT/time_$impl.txt"
  done
done
