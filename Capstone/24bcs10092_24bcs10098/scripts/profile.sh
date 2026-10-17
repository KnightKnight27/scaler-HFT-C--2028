#!/bin/sh
# Run only after the profiling build and fixture regression checks succeed.
# Counter/profile files stay in the ignored build-profile directory.
set -u
cd "$(dirname "$0")/.." || exit 1
out=build-profile/evidence
mkdir -p "$out"
export LC_ALL=C
# Use local symbols only; optional Ubuntu debuginfod lookup stalled reporting.
export DEBUGINFOD_URLS=
status=0
for impl in A B C; do
    echo "STAT requested events: $impl (five repetitions)"
    perf stat -r 5 -x ';' -o "$out/stat-$impl.csv" \
      -e 'task-clock,cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u,L1-dcache-load-misses:u,dTLB-load-misses:u,page-faults' \
      -- ./build-profile/bench --impl "$impl" --ops 1000000 --runs 1 --seed 42 --profile-only \
      > "$out/stat-$impl-program.log" 2> "$out/stat-$impl-errors.log"
    rc=$?
    echo "stat $impl exit=$rc"
    if test "$rc" -ne 0; then status=1; fi
    echo "STAT paired denominators: $impl (five repetitions)"
    perf stat -r 5 -x ';' -o "$out/paired-$impl.csv" \
      -e '{branches:u,branch-misses:u},{cache-references:u,cache-misses:u},{L1-dcache-loads:u,L1-dcache-load-misses:u},{dTLB-loads:u,dTLB-load-misses:u}' \
      -- ./build-profile/bench --impl "$impl" --ops 1000000 --runs 1 --seed 42 --profile-only \
      > "$out/paired-$impl-program.log" 2> "$out/paired-$impl-errors.log"
    rc=$?
    echo "paired $impl exit=$rc"
    if test "$rc" -ne 0; then status=1; fi
    echo "RECORD cycles/user call graph: $impl"
    perf record -g -F 499 -e cycles:u -o "$out/perf-$impl.data" \
      -- ./build-profile/bench --impl "$impl" --ops 1000000 --runs 1 --seed 42 --profile-only \
      > "$out/record-$impl-program.log" 2> "$out/record-$impl-errors.log"
    rc=$?
    echo "record $impl exit=$rc"
    if test "$rc" -ne 0; then status=1; fi
    if test "$rc" -eq 0; then
        perf report --stdio --no-children --call-graph none --sort dso,symbol --percent-limit 0.5 \
          -i "$out/perf-$impl.data" > "$out/report-$impl-self.txt" 2> "$out/report-$impl-errors.log"
        rc=$?
        echo "self report $impl exit=$rc"
        if test "$rc" -ne 0; then status=1; fi
        perf report --stdio --children --call-graph graph,1,caller --percent-limit 1 --max-stack 32 \
          -i "$out/perf-$impl.data" > "$out/report-$impl-stacks.txt" 2>> "$out/report-$impl-errors.log"
        rc=$?
        echo "stack report $impl exit=$rc"
        if test "$rc" -ne 0; then status=1; fi
    fi
done
exit "$status"
