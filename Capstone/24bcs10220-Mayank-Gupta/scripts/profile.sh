#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
if [ "$(uname -s)" != Linux ]; then
    echo "Run this script inside Linux; macOS cannot run Linux perf." >&2
    exit 1
fi
command -v perf >/dev/null
if [ ! -x ./bench ]; then
    echo "Build ./bench on Linux before profiling." >&2
    exit 1
fi

out=${OUT:-results/linux-perf}
events=${EVENTS:-task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults}
record_event=${RECORD_EVENT:-cycles}
mkdir -p "$out"
{
    date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
    uname -smr
    g++ --version
    perf --version
    printf 'stat events: %s\nrecord event: %s\n' "$events" "$record_event"
} > "$out/environment.txt"

for impl in A B C; do
    perf stat -r 5 -e "$events" -o "$out/$impl-stat.txt" \
        -- ./bench --impl "$impl" --profile > "$out/$impl-stat-program.txt"
    perf record -e "$record_event" -g -o "$out/$impl-perf.data" \
        -- ./bench --impl "$impl" --profile > "$out/$impl-record-program.txt" \
        2> "$out/$impl-record-log.txt"
    perf report --stdio --no-children -i "$out/$impl-perf.data" \
        > "$out/$impl-report.txt" 2> "$out/$impl-report-log.txt"
done
