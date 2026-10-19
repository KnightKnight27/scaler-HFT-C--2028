#!/usr/bin/env bash
# macOS only.  Reads the M2 hardware counters for A, B, C.  No baseline run
# is needed because counting starts and stops around the op loop only.
#
#   sudo ./scripts/run_apple_pmu.sh
set -euo pipefail
cd "$(dirname "$0")/.."
make -s apple-pmu
mkdir -p results
{
  echo "# $(date -u)  $(sysctl -n machdep.cpu.brand_string)  macOS $(sw_vers -productVersion)  $(c++ --version | head -1)"
} > results/machine_macos.txt
for impl in A B C; do
  ./build/apple_pmu --impl "$impl" --reps 5 | tee "results/apple_pmu_$impl.txt"
  echo
done
