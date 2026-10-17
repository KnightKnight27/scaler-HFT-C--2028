#!/bin/bash
# verify + benchmarks on the mac, and cpu counters from /usr/bin/time -l
# use clang++ here, homebrew g++ steady_clock only ticks every 1 us on mac
set -e
cd "$(dirname "$0")"
mkdir -p results

clang++ -std=c++17 -O2 -Wall -Wextra bench.cpp -o bench

./bench --verify > results/verify.txt

{
    echo "$(sysctl -n machdep.cpu.brand_string), macOS $(sw_vers -productVersion), $(clang++ --version | head -1)"
    echo
    for impl in A B C; do
        ./bench --impl $impl
        echo
    done
} > results/bench.txt

for impl in A B C none; do
    for i in 1 2 3 4 5; do
        /usr/bin/time -l ./bench --impl $impl --perf
    done
done > results/mac_time.txt 2>&1

# real cpu counters with Instruments. "OB Counters" is a CPU Counters template we saved in Instruments
# with 6 events: FIXED_CYCLES, BRANCH_MISPRED_NONSPEC, FIXED_INSTRUCTIONS, INST_BRANCH,
# L1D_CACHE_MISS_LD_NONSPEC, L1D_TLB_MISS. Every 1 ms sample has those 6 values, so we add them up.
{
    echo "impl cycles branch_mispred instructions branches l1d_load_miss dtlb_miss"
    for impl in A B C none; do
        for i in 1 2 3 4 5; do
            rm -rf /tmp/ob.trace
            xctrace record --quiet --no-prompt --template "OB Counters" --output /tmp/ob.trace \
                --launch -- ./bench --impl $impl --perf > /dev/null 2>&1
            printf "%s " $impl
            xctrace export --input /tmp/ob.trace --xpath '/trace-toc/run[@number="1"]/data/table[@schema="counters-profile"]' |
                grep -oE '<pmc-events id="[0-9]+" fmt="[^"]*">[^<]*' | sed 's/.*>//' |
                awk '{ for (i = 1; i <= 6; i++) s[i] += $i } END { print s[1], s[2], s[3], s[4], s[5], s[6] }'
        done
    done
} > results/mac_counters.txt
