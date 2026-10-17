#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p results
make -B

{
    date -u
    uname -a
    cat /etc/os-release
    g++ --version
    perf --version
    lscpu
    cat /proc/sys/kernel/perf_event_paranoid
    sha256sum bench.cpp order_book.cpp order_book.h
} > results/environment.txt
ls /sys/bus/event_source/devices > results/perf-event-sources.txt

./bench --verify > results/verify.txt

for impl in A B C; do
    ./bench --impl "$impl" > "results/bench-$impl.txt"
done

events=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

for impl in A B C; do
    if perf stat -r 5 -e "$events" -o "results/perf-stat-$impl.txt" \
        ./bench --impl "$impl" --profile > "results/perf-replay-$impl.txt" \
        2> "results/perf-stat-$impl.stderr"; then
        echo "perf stat $impl completed"
    else
        echo "perf stat $impl failed; see results/perf-stat-$impl.stderr"
    fi

    if perf record -F 9999 -e task-clock:u -g -o "/tmp/prabhav-perf-$impl.data" \
        ./bench --impl "$impl" --profile > "results/record-replay-$impl.txt" \
        2> "results/perf-record-$impl.txt"; then
        perf report --stdio --no-children --percent-limit 1 \
            -i "/tmp/prabhav-perf-$impl.data" > "results/perf-report-$impl.txt" \
            2> "results/perf-report-$impl.stderr"
    else
        echo "perf record $impl failed; see results/perf-record-$impl.txt"
    fi

    /usr/bin/time -v ./bench --impl "$impl" --profile \
        > "results/time-replay-$impl.txt" 2> "results/time-$impl.txt"
done
