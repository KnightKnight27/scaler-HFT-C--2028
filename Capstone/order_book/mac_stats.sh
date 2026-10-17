#!/bin/bash
# perf is not on macOS, but /usr/bin/time -l gives a few real counters
# (instructions, cycles, page reclaims, max memory) so I used it on my Mac
# usage: ./mac_stats.sh

g++ -O2 -std=c++20 -Wall -o bench bench.cpp

for impl in A B C; do
    echo "impl $impl"
    /usr/bin/time -l ./bench --impl $impl --runs 1 2>&1 > /dev/null | grep -E "real|maximum resident|page reclaims|page faults|instructions retired|cycles elapsed"
    echo
done
