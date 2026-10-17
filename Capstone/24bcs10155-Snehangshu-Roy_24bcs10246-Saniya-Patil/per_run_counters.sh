#!/usr/bin/env bash
# perf stat for the whole program also counts generating the workload, the
# final replay and process startup. To get counters for the book alone we run
# --runs 1 and --runs 5 (both --no-timers) and take (runs5 - runs1) / 4 = cost
# of one run (one pass over the 1M ops on a fresh book). Everything that is the
# same for --runs 1 and --runs 5 cancels out.
set -e
cd "$(dirname "$0")"
make -s bench
EVENTS=cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

get() {  # impl runs -> "event value" lines
    # without root perf only counts user space and calls the events cycles:u etc.
    perf stat -x, -r 5 -e $EVENTS ./bench --impl $1 --runs $2 --no-timers 2>&1 >/dev/null |
        awk -F, '{ sub(/:u$/, "", $3); print $3, $1 }'
}

printf "%-24s %14s %14s %14s\n" "per run (1M ops)" A B C
for impl in A B C; do
    get $impl 1 > "$tmp/r1_$impl"
    get $impl 5 > "$tmp/r5_$impl"
done
for ev in ${EVENTS//,/ }; do
    printf "%-24s" $ev
    for impl in A B C; do
        a=$(awk -v e=$ev '$1==e{print $2}' "$tmp/r1_$impl")
        b=$(awk -v e=$ev '$1==e{print $2}' "$tmp/r5_$impl")
        # n/a if perf gave no number (<not supported>), and 0 instead of -0
        awk -v a="$a" -v b="$b" 'BEGIN {
            if (a !~ /^[0-9.]+$/ || b !~ /^[0-9.]+$/) { printf " %14s", "n/a"; exit }
            s = sprintf("%.0f", (b - a) / 4)
            if (s == "-0") s = "0"
            printf " %14s", s
        }'
    done
    echo
done
