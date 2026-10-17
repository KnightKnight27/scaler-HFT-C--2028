#!/bin/bash
# Script to run Linux perf stat profiling on all three order book implementations
# As required by Section 8 of the Capstone Assignment

set -e

echo "Compiling benchmark with -O2..."
make clean
make

echo "=========================================================="
echo " 1. Running Correctness Verification"
echo "=========================================================="
./bench --verify

echo ""
echo "=========================================================="
echo " 2. Profiling Implementation A with Linux perf stat"
echo "=========================================================="
if command -v perf &> /dev/null; then
    perf stat -r 5 \
      -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
      ./bench --impl A
else
    echo "Warning: 'perf' command not found on this system (macOS / non-Linux)."
    echo "Running native benchmark directly:"
    ./bench --impl A
fi

echo ""
echo "=========================================================="
echo " 3. Profiling Implementation B with Linux perf stat"
echo "=========================================================="
if command -v perf &> /dev/null; then
    perf stat -r 5 \
      -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
      ./bench --impl B
else
    echo "Warning: 'perf' command not found on this system (macOS / non-Linux)."
    echo "Running native benchmark directly:"
    ./bench --impl B
fi

echo ""
echo "=========================================================="
echo " 4. Profiling Implementation C with Linux perf stat"
echo "=========================================================="
if command -v perf &> /dev/null; then
    perf stat -r 5 \
      -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
      ./bench --impl C
else
    echo "Warning: 'perf' command not found on this system (macOS / non-Linux)."
    echo "Running native benchmark directly:"
    ./bench --impl C
fi

echo ""
echo "Profiling complete."
