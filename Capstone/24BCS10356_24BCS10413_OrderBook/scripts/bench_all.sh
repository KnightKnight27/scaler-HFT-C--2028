#!/usr/bin/env bash
# Build, run the tests and --verify, then benchmark A, B and C (5 runs each,
# median reported by bench itself), saving every output plus a machine summary.
#
# Usage: ./scripts/bench_all.sh [outdir]          (default: results/local)
#        PIN="taskset -c 3" ./scripts/bench_all.sh results/x86   (pin to one core)
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results/local}
mkdir -p "$OUT"
make -s bench test_orderbook

./test_orderbook | tee "$OUT/tests.txt"
./bench --verify | tee "$OUT/verify.txt"
for IMPL in A B C; do
  ${PIN:-} ./bench --impl "$IMPL" | tee "$OUT/bench_$IMPL.txt"
done

{
  uname -srm
  lscpu 2>/dev/null | grep -E 'Model name|^CPU\(s\)|L1d|L2|L3' || sysctl -n machdep.cpu.brand_string
  "${CXX:-g++}" --version | head -1
  echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
  echo "pinning: ${PIN:-none}"
  echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo n/a)"
} | tee "$OUT/machine.txt"
