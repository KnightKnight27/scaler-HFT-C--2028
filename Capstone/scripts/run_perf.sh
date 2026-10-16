#!/bin/bash
set -e

# Build the project with -O2
echo "=== Building project with -O2 ==="
make clean
make CXXFLAGS="-O2 -std=c++17 -Wall -Wextra -Wpedantic -Iinclude"

echo ""
echo "=== Running Correctness Verification ==="
./bench --verify

echo ""
echo "=========================================="
echo "=== Profiling Implementation A with perf ==="
echo "=========================================="
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

echo ""
echo "=========================================="
echo "=== Profiling Implementation B with perf ==="
echo "=========================================="
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl B

echo ""
echo "=========================================="
echo "=== Profiling Implementation C with perf ==="
echo "=========================================="
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl C

echo ""
echo "=== Recording Callgraphs with perf record ==="
perf record -g -o perf_A.data ./bench --impl A
perf report -i perf_A.data --stdio > perf_report_A.txt

perf record -g -o perf_B.data ./bench --impl B
perf report -i perf_B.data --stdio > perf_report_B.txt

perf record -g -o perf_C.data ./bench --impl C
perf report -i perf_C.data --stdio > perf_report_C.txt

echo "=== All perf runs and reports complete! ==="
