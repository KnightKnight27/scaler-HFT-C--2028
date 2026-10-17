#!/usr/bin/env bash
# mac side: build, verify and run the timed benchmark natively. perf is linux only, see run_perf.sh.
# we build with apple clang here on purpose, its steady_clock ticks every ~42 ns, homebrew gcc's every 1 us on macOS.
set -u
cd "$(dirname "$0")/.."
OUT=results/macos
mkdir -p $OUT

CXX=${CXX:-/usr/bin/g++}
$CXX -O2 -std=c++17 -Wall -Wextra -Wpedantic bench.cpp workload.cpp -o bench || exit 1
$CXX -O2 -std=c++17 -Wall -Wextra -Wpedantic tests/edge_cases.cpp -o edge_tests || exit 1
{ sysctl -n machdep.cpu.brand_string; sw_vers | head -2; $CXX --version | head -1; } > $OUT/env.txt
cat $OUT/env.txt

./bench --verify | tee $OUT/verify.txt
./edge_tests | tee -a $OUT/verify.txt

for X in A B C; do
    ./bench --impl $X --runs 5 --json > $OUT/bench_$X.json
    ./bench --impl $X --runs 5 | tee $OUT/bench_$X.txt
done
