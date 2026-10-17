#!/bin/bash
# usage (Linux):  ./run_perf.sh
# usage (macOS):  docker run --rm --privileged -v "$PWD":/work -w /work debian:bookworm bash -c \
#                   "apt-get update -qq && apt-get install -y -qq g++ make linux-perf >/dev/null && ./run_perf.sh"
set -e
# build in /tmp: perf can't read symbols from the docker-desktop bind mount
R="$(cd "$(dirname "$0")" && pwd)/results"
mkdir -p $R
rm -rf /tmp/ob && cp -r "$(dirname "$0")/OrderBook" /tmp/ob && cd /tmp/ob
make clean >/dev/null && make >/dev/null
./bench --verify | tee $R/verify.txt
for X in A B C; do
    ./bench --impl $X > $R/bench_$X.txt
    ./bench --impl $X --untimed > $R/bench_untimed_$X.txt
    perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
        ./bench --impl $X > /dev/null 2> $R/perf_stat_$X.txt
    perf record -F 20000 -g -o /tmp/perf_$X.data ./bench --impl $X --untimed --runs 20 > /dev/null 2>&1
    perf report -i /tmp/perf_$X.data --stdio --no-children --sort dso,symbol --percent-limit 1 -g none 2>/dev/null \
        | grep -v '^#' | grep -v '^$' | sed 's/ *- *- *$//' > $R/perf_report_$X.txt
    cat $R/perf_stat_$X.txt
done
