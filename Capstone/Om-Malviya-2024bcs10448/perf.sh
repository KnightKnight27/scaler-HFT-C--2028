#!/usr/bin/env bash
# Linux perf profiling for the three order-book implementations.
#
# perf is a Linux tool and is NOT available on macOS, so this script is intended
# to be run on a Linux (lab) machine. Build first:
#     g++ -O2 -std=c++17 bench.cpp -o bench
# then:
#     bash perf.sh
#
# If some hardware counters are unavailable in your environment (VMs/containers
# often restrict the PMU), perf prints "<not supported>" for those; report the
# counters that do work and note the ones that did not, per the assignment FAQ.
set -u
BIN=./bench
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

echo "========================= perf stat ========================="
for impl in A B C; do
  echo "----- Implementation ${impl} -----"
  perf stat -r 5 -e "${EVENTS}" "${BIN}" --impl "${impl}" --runs 1
  echo
done

echo "======================= perf record ========================="
for impl in A B C; do
  echo "----- Implementation ${impl} -----"
  perf record -g -o "perf_${impl}.data" "${BIN}" --impl "${impl}" --runs 1
  perf report -i "perf_${impl}.data" --stdio | head -40
  echo
done
