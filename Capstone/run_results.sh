#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p results
export DEBUGINFOD_URLS=

flags=(-std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic)
g++ "${flags[@]}" bench.cpp -o bench
{
    date -Iseconds
    uname -a
    g++ --version
    perf --version
    lscpu
    printf '\nBuild: g++ %s bench.cpp -o bench\n' "${flags[*]}"
    printf 'perf_event_paranoid: '
    cat /proc/sys/kernel/perf_event_paranoid
    taskset -pc $$
} > results/environment.txt

./bench --verify | tee results/verify.txt
./bench --verify --seed 7 --ops 10000 > results/verify-seed7.txt

for impl in A B C; do
    ./bench --impl "$impl" --csv "results/bench-$impl.csv" | tee "results/bench-$impl.txt"
done

events=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
supported=()
: > results/counter-availability.txt
for event in ${events//,/ }; do
    if perf stat -e "$event" -- true > /dev/null 2> results/counter-probe.txt; then
        supported+=("$event")
        printf '%s: available\n' "$event" >> results/counter-availability.txt
    else
        printf '%s: unavailable\n' "$event" >> results/counter-availability.txt
        cat results/counter-probe.txt >> results/counter-availability.txt
    fi
done
rm -f results/counter-probe.txt
available=$(IFS=,; echo "${supported[*]}")

for impl in A B C; do
    rm -f "results/perf-stat-$impl-available.txt" "results/perf-stat-$impl-available-stdout.txt" \
          "results/perf-record-$impl-software.txt" "results/perf-record-$impl-software-stdout.txt"
    # Five process repetitions, each replaying five books, amortize workload setup.
    if ! perf stat -r 5 -e "$events" -- ./bench --impl "$impl" --profile \
            > "results/perf-stat-$impl-stdout.txt" 2> "results/perf-stat-$impl.txt"; then
        if [[ -n "$available" ]]; then
            perf stat -r 5 -e "$available" -- ./bench --impl "$impl" --profile \
                > "results/perf-stat-$impl-available-stdout.txt" \
                2> "results/perf-stat-$impl-available.txt"
        fi
    fi
    # Try the normal cycles sampler, then a software sampler for VMs without PMU.
    # perf's binary writer can fail on WSL's Windows mount, so write in /tmp.
    data_file=$(mktemp /tmp/orderbook-perf-XXXXXX.data)
    if ! perf record -g -o "$data_file" -- ./bench --impl "$impl" --profile \
            > "results/perf-record-$impl-stdout.txt" 2> "results/perf-record-$impl.txt"; then
        perf record -e cpu-clock -F 499 -g -o "$data_file" -- \
            ./bench --impl "$impl" --profile \
            > "results/perf-record-$impl-software-stdout.txt" \
            2> "results/perf-record-$impl-software.txt"
    fi
    perf report --stdio --no-children --call-graph none --percent-limit 1 --sort comm,dso,symbol \
        -i "$data_file" > "results/perf-report-$impl.txt"
    mv "$data_file" "results/perf-$impl.data"
done
