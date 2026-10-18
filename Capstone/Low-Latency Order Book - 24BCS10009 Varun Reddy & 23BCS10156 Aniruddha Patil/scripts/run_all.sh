#!/usr/bin/env bash
# Reproduces every result in REPORT.md. Run from the repository root after building.
#   ./scripts/run_all.sh [all|verify|bench|stat|record]
# perf needs: sudo sysctl kernel.perf_event_paranoid=1
set -eu

BENCH=./build/bench
OUT=results
CPU=${CPU:-2}                     # pin to one core to reduce noise
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
WHAT=${1:-all}
mkdir -p "$OUT"

if [[ $WHAT == all || $WHAT == verify ]]; then
    echo "== correctness"
    $BENCH --verify | tee "$OUT/verify.txt"
fi

if [[ $WHAT == all || $WHAT == bench ]]; then
    echo "== benchmarks (5 repetitions each, median)"
    for impl in A B C; do
        taskset -c "$CPU" $BENCH --impl "$impl" | tee "$OUT/bench_$impl.txt"
    done
    # Same benchmark with std::chrono::steady_clock as the per-op timer, for comparison.
    taskset -c "$CPU" $BENCH --impl C --reps 1 --timer steady > "$OUT/bench_C_steady_timer.txt"
fi

if [[ $WHAT == all || $WHAT == stat ]]; then
    echo "== perf stat (one workload pass per process, 5 processes; 'none' = harness baseline)"
    for impl in none A B C; do
        perf stat -r 5 -e "$EVENTS" -o "$OUT/perf_stat_$impl.txt" \
            taskset -c "$CPU" $BENCH --impl "$impl" --reps 1 --no-latency > /dev/null
        cat "$OUT/perf_stat_$impl.txt"
    done
fi

if [[ $WHAT == all || $WHAT == record ]]; then
    echo "== perf record"
    for impl in A B C; do
        data="$OUT/perf_$impl.data"
        perf record -g -F 20000 -o "$data" taskset -c "$CPU" $BENCH --impl "$impl" --reps 1 --no-latency > /dev/null 2>&1
        perf report -i "$data" --no-children --percent-limit 1 --stdio -g none 2>/dev/null \
            | grep -v '^$' > "$OUT/perf_report_$impl.txt" || true
        perf report -i "$data" --children --percent-limit 5 --stdio -g caller,0.5,callee --max-stack 8 2>/dev/null \
            | grep -v '^$' > "$OUT/perf_callgraph_$impl.txt" || true
        rm -f "$data" "$data.old"
        grep '%' "$OUT/perf_report_$impl.txt" | head -25
    done
fi
