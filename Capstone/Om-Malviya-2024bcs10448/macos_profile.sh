#!/usr/bin/env bash
# Local profiling substitute for macOS, where Linux perf is unavailable.
#
# /usr/bin/time -l reports instructions retired, cycles elapsed (-> IPC),
# peak memory footprint, page faults, and context switches for the whole
# process. Workload generation is identical across A/B/C, so the differences
# reflect the implementations. Build first:
#     g++ -O2 -std=c++17 bench.cpp -o bench
# then:
#     bash macos_profile.sh
set -u
BIN=./bench
for impl in A B C; do
  echo "===== /usr/bin/time -l ${BIN} --impl ${impl} --runs 1 ====="
  /usr/bin/time -l "${BIN}" --impl "${impl}" --runs 1 >/dev/null
  echo
done
