#!/bin/sh
# Run on a Linux machine with perf installed. Collects everything the report needs into results/.
set -e
cd "$(dirname "$0")"

mkdir -p results
make bench test_a test_b test_c

echo "== unit tests ==" | tee results/tests.txt
./test_a | tee -a results/tests.txt
./test_b | tee -a results/tests.txt
./test_c | tee -a results/tests.txt

echo "== verify ==" | tee results/verify.txt
./bench --verify | tee -a results/verify.txt

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

# Same code, built with symbols and without PIE so sampled addresses map straight to functions.
g++ -std=c++20 -O2 -g -fno-omit-frame-pointer -no-pie -fno-pie -Iinclude src/bench.cpp -o /tmp/bench_perf

for IMPL in A B C; do
    echo "== bench $IMPL ==" | tee results/bench_$IMPL.txt
    ./bench --impl $IMPL | tee -a results/bench_$IMPL.txt

    echo "== perf stat $IMPL ==" | tee results/perf_stat_$IMPL.txt
    perf stat -r 5 -e $EVENTS -o /tmp/perf_stat_$IMPL.txt ./bench --impl $IMPL --runs 1 > /dev/null
    cat /tmp/perf_stat_$IMPL.txt | tee -a results/perf_stat_$IMPL.txt

    echo "== perf record $IMPL: where the samples land ==" | tee results/perf_report_$IMPL.txt
    perf record -g -o /tmp/perf_$IMPL.data /tmp/bench_perf --impl $IMPL --runs 1 > /dev/null 2>&1
    perf script -i /tmp/perf_$IMPL.data -F ip,dso 2>/dev/null > /tmp/perf_$IMPL.samples
    TOTAL=$(wc -l < /tmp/perf_$IMPL.samples)
    echo "total samples: $TOTAL" | tee -a results/perf_report_$IMPL.txt
    echo "by shared object:" | tee -a results/perf_report_$IMPL.txt
    awk '{print $2}' /tmp/perf_$IMPL.samples | sort | uniq -c | sort -rn | head -5 | tee -a results/perf_report_$IMPL.txt
    echo "top functions inside bench (innermost inlined frame):" | tee -a results/perf_report_$IMPL.txt
    grep bench_perf /tmp/perf_$IMPL.samples | awk '{print $1}' | sort | uniq -c | sort -rn | head -40 \
        | while read N IP; do
            FN=$(addr2line -e /tmp/bench_perf -f -C -i "0x$IP" | head -1)
            echo "$N $FN"
          done | awk '{n=$1; $1=""; s[$0]+=n} END {for (k in s) printf "%7d  %s\n", s[k], k}' | sort -rn | head -15 \
        | tee -a results/perf_report_$IMPL.txt
done

echo
echo "Done. Outputs are in results/. Copy perf_stat_*.txt into REPORT.md."
