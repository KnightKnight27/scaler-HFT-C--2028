#!/usr/bin/env bash
# Builds and profiles all three implementations on Linux. Writes everything to results/.
# Run from the project folder on a Linux box, or inside Docker on a Mac:
#   docker run --rm --platform linux/arm64 --privileged -v "$PWD":/ob -w /ob ubuntu:24.04 \
#     bash -c 'apt-get update -qq && apt-get install -y -qq g++ make linux-tools-generic valgrind >/dev/null && scripts/perf_linux.sh'
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=results
mkdir -p "$OUT"

# Ubuntu's perf wrapper refuses to run on a kernel it doesn't know (e.g. Docker's linuxkit),
# so fall back to the real binary directly.
PERF=perf
if ! perf --version >/dev/null 2>&1; then PERF=$(ls /usr/lib/linux-tools/*/perf | sed -n 1p); fi
echo -1 > /proc/sys/kernel/perf_event_paranoid 2>/dev/null || true

make -s clean bench bench_prof
{ uname -srm; g++ --version | sed -n 1p; $PERF --version; nproc | sed 's/^/cpus: /'; } > "$OUT/linux_env.txt"

./bench --verify > "$OUT/verify_linux.txt"
./bench --impl all --runs 5 > "$OUT/bench_linux.txt"

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
: > "$OUT/perf_stat.txt"
# "none" only generates the workload: subtract it to see what the book itself costs.
for impl in none A B C; do
  echo "===== perf stat -r 5 ./bench --impl $impl --runs 1 --total-only =====" >> "$OUT/perf_stat.txt"
  $PERF stat -r 5 -e "$EVENTS" ./bench --impl "$impl" --runs 1 --total-only >/dev/null 2>> "$OUT/perf_stat.txt"
done

# Sampling profile. Hardware cycle sampling is not available in a VM, so sample on cpu-clock.
for impl in A B C; do
  $PERF record -q -e cpu-clock -F 20000 --call-graph fp -o "perf.$impl.data" ./bench_prof --impl "$impl" --runs 3 --total-only >/dev/null 2>&1
  # Flat profile (self time per function); long template names are cut at 150 chars.
  $PERF report -i "perf.$impl.data" --stdio --no-children -g none --percent-limit 0.5 2>/dev/null \
    | grep -v '^$' | grep -v '^# \.\.\.' | cut -c1-150 > "$OUT/perf_report_$impl.txt"
  rm -f "perf.$impl.data"
done

# Simulated cache + branch counters (exact instruction counts; caches modelled on an
# Apple P-core: 128 KB 8-way L1D, 16 MB L2 as last level).
: > "$OUT/cachegrind.txt"
for impl in none A B C; do
  echo "===== cachegrind ./bench --impl $impl --runs 1 --total-only =====" >> "$OUT/cachegrind.txt"
  valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
    --I1=131072,8,64 --D1=131072,8,64 --LL=16777216,16,64 \
    --cachegrind-out-file=/dev/null ./bench --impl "$impl" --runs 1 --total-only 2>&1 >/dev/null \
    | sed 's/^==[0-9]*== //' | grep -E 'refs|misses|rate|Mispred|Branches' >> "$OUT/cachegrind.txt"
done
echo "done: see $OUT/"
