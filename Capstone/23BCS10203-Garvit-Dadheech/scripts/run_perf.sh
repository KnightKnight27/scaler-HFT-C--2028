#!/usr/bin/env bash
# perf stat + perf record for A, B, C and the "none" baseline.  Needs Linux
# (natively or the docker container from README.md).  Writes into results/.
#
# --no-timers keeps std::chrono out of the measured code.  perf record uses
# --reps 3 and the -DOB_PROFILE build (public methods not inlined) so the
# profile is mostly book code and the methods show up by name.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
SW_EVENTS=task-clock,context-switches,cpu-migrations,page-faults,minor-faults,major-faults

{
  echo "# $(date -u)  kernel $(uname -r)  $(g++ --version | head -1)"
  echo "# $(grep -m1 'model name' /proc/cpuinfo || echo 'cpu: (model name unavailable)')  perf $(perf --version)"
} > results/machine_linux.txt

# ---- perf stat: normal -O2 build -----------------------------------------
BUILD=build-linux
make -s -B BUILD="$BUILD" CXX=g++ CXXFLAGS="-O2 -std=c++17"
BENCH="./$BUILD/bench"

for impl in A B C none; do
  echo "== perf stat, implementation $impl =="
  perf stat -o "results/perf_stat_$impl.txt" -r 5 -e "$EVENTS" \
      "$BENCH" --impl "$impl" --reps 1 --no-timers > /dev/null || true
  perf stat -o "results/perf_stat_sw_$impl.txt" -r 5 -e "$SW_EVENTS" \
      "$BENCH" --impl "$impl" --reps 1 --no-timers > /dev/null || true
  grep -E "task-clock|cycles|instructions|branch|cache|TLB|faults|elapsed" "results/perf_stat_$impl.txt" || true
done

# ---- timed benchmark + verify from the same Linux environment --------------
for impl in A B C none; do "$BENCH" --impl "$impl" > "results/bench_${impl}_linux.txt"; done
"$BENCH" --verify > results/verify_linux.txt

# ---- perf record: profiling build (-g, frame pointers, API not inlined) ----
BUILD=build-linux-profile
make -s -B BUILD="$BUILD" CXX=g++ CXXFLAGS="-O2 -std=c++17 -g -fno-omit-frame-pointer -DOB_PROFILE"
BENCH="./$BUILD/bench"

for impl in A B C; do
  echo "== perf record, implementation $impl =="
  perf record -F 20000 -g -o "perf_$impl.data" "$BENCH" --impl "$impl" --reps 3 --no-timers > /dev/null 2>&1 || true
  perf report -i "perf_$impl.data" --stdio --no-children -g caller --percent-limit 2 \
      > "results/perf_report_$impl.txt" 2>&1 || true
  perf report -i "perf_$impl.data" --stdio --no-children -g none --percent-limit 0.5 \
      > "results/perf_report_flat_$impl.txt" 2>&1 || true
  rm -f "perf_$impl.data"
done
echo "done"
