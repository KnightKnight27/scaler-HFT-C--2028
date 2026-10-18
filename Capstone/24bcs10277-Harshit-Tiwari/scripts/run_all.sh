#!/usr/bin/env bash
# Build, verify, benchmark and profile A, B and C. Output goes to results/.
set -e
cd "$(dirname "$0")/.."
PERF=${PERF:-perf}
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
DATA=/tmp/perf_orderbook.data  # perf cannot write perf.data on /mnt/c under WSL

make
mkdir -p results

./bench --verify | tee results/verify.txt

# The machine was shared with other work, so the three are interleaved.
rm -f results/bench_*.txt
for round in 1 2 3; do
    for impl in A B C; do
        echo "### round $round" >> results/bench_$impl.txt
        ./bench --impl $impl | tee -a results/bench_$impl.txt
    done
done

for impl in A B C; do
    $PERF stat -r 5 -e $EVENTS -o results/perf_stat_$impl.txt ./bench --impl $impl --runs 1 > /dev/null

    $PERF record -g -o $DATA ./bench_prof --impl $impl --runs 3 > /dev/null
    {
        $PERF report -i $DATA --stdio --no-children --sort symbol -g none --percent-limit 0.5
        $PERF report -i $DATA --stdio --no-children --sort symbol -g caller,0.5,callee --percent-limit 3
    } > results/perf_report_$impl.txt
done
rm -f $DATA $DATA.old

# Extra experiments for the report: a book that does nothing (harness cost)
# and book C with its order table pre-sized to 2^21 slots.
TMP=/tmp/orderbook_exp
rm -rf $TMP && mkdir -p $TMP/null $TMP/presize
cp include/*.hpp $TMP/null/
cp include/*.hpp $TMP/presize/
cat > $TMP/null/book_array.hpp <<'EOF'
#pragma once
#include "common.hpp"
class ArrayBook {
public:
    bool add_order(uint64_t, Side, double, long) { return true; }
    bool cancel_order(uint64_t) { return true; }
    BBO get_bbo() const { return BBO{false, 0, 0, false, 0, 0}; }
    std::vector<Level> get_bids(int = 10) const { return {}; }
    std::vector<Level> get_asks(int = 10) const { return {}; }
};
EOF
sed -i 's/rebuild(1 << 16)/rebuild(1 << 21)/' $TMP/presize/book_array.hpp

for variant in null presize; do
    g++ -std=c++17 -O2 -I$TMP/$variant -Isrc src/bench.cpp -o $TMP/bench_$variant
    echo "################ $variant"
    $TMP/bench_$variant --impl C | sed -n '/Total time/,/bids+asks/p'
    $PERF stat -r 5 -e $EVENTS $TMP/bench_$variant --impl C --runs 1 2>&1 >/dev/null
done | tee results/experiments.txt
rm -rf $TMP
