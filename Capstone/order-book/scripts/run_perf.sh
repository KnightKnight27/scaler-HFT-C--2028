#!/usr/bin/env bash
# Linux: build, verify, then perf stat + perf record for A, B, C and the baseline.
# Output goes to results/linux/. Run from the order-book directory:
#   ./scripts/run_perf.sh
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=results/linux
mkdir -p "$OUT"

g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -o bench src/bench.cpp
./bench --verify | tee "$OUT/verify.txt"

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

# perf record needs a sampling event; fall back to the cpu-clock software
# timer when hardware cycles are not exposed (VMs, containers on Apple Silicon).
SAMPLE_EVENT=cycles
if perf stat -e cycles true 2>&1 | grep -q "not supported"; then SAMPLE_EVENT=cpu-clock; fi
echo "perf record sampling event: $SAMPLE_EVENT" | tee "$OUT/perf_env.txt"
uname -a >> "$OUT/perf_env.txt"
perf --version >> "$OUT/perf_env.txt"

for impl in A B C 0; do
  ./bench --impl "$impl" > "$OUT/bench_$impl.txt"
  perf stat -r 5 -e "$EVENTS" ./bench --impl "$impl" > /dev/null 2> "$OUT/perf_stat_$impl.txt"
  cat "$OUT/perf_stat_$impl.txt"
  if [ "$impl" != 0 ]; then
    # Same counters without per-op clock_gettime calls (book work only).
    perf stat -r 5 -e "$EVENTS" ./bench --impl "$impl" --untimed > /dev/null 2> "$OUT/perf_stat_${impl}_untimed.txt"
    cat "$OUT/perf_stat_${impl}_untimed.txt"
    DATA=$(mktemp)
    perf record -e "$SAMPLE_EVENT" -g -o "$DATA" ./bench --impl "$impl" --untimed --runs 1 > /dev/null 2>&1
    # `head` closes the pipe early; don't let SIGPIPE trip set -e/pipefail.
    { perf report -i "$DATA" --stdio --no-children --percent-limit 1.5 2>/dev/null \
        | grep -v '^$' | head -150 > "$OUT/perf_report_$impl.txt"; } || true
    rm -f "$DATA"
  fi
done
