#!/usr/bin/env bash
# Collect perf stat + perf record/report for all three implementations (Linux only).
# Usage: ./scripts/perf_all.sh [outdir]     (from the project root, after `make`;
#                                            default outdir: results)
#
# Uses `./bench --impl X --perf`: one uninstrumented pass, so the counters
# measure the order book rather than the per-call timers. The exact command
# from the assignment (`./bench --impl X`, 5 timed runs) also works with perf.
# It also records `./bench --gen-only` (workload generation alone, which every
# --perf run pays for too) so that cost can be subtracted from each column.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-results}
mkdir -p "$OUT"
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

# Fail early with the fix instead of five runs of permission errors.
if ! perf stat -e task-clock true >/dev/null 2>&1; then
  echo "perf cannot open events (kernel.perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo '?'))." >&2
  echo "Lower it with: sudo sysctl kernel.perf_event_paranoid=1   (or run on a lab machine)" >&2
  exit 1
fi

for IMPL in A B C; do
  echo "=== perf stat: impl $IMPL ==="
  perf stat -r 5 -e "$EVENTS" ./bench --impl "$IMPL" --perf 2>&1 | tee "$OUT/perf_stat_$IMPL.txt"

  echo "=== perf record: impl $IMPL ==="
  perf record -g -o "$OUT/perf_$IMPL.data" ./bench --impl "$IMPL" --perf >/dev/null 2>&1
  # Write the full report first, then keep the top part. Piping straight into
  # `head` would kill perf with SIGPIPE and, with pipefail, stop the script.
  perf report -i "$OUT/perf_$IMPL.data" --stdio --no-children > "$OUT/perf_report_${IMPL}_full.txt" 2>/dev/null
  sed -n '1,120p' "$OUT/perf_report_${IMPL}_full.txt" > "$OUT/perf_report_$IMPL.txt"
  rm -f "$OUT/perf_report_${IMPL}_full.txt"
  # The call-graph excerpt above only reaches the top few symbols, so also
  # save a flat profile (self time per symbol, every symbol >= 0.5 %).
  perf report -i "$OUT/perf_$IMPL.data" --stdio --no-children -g none --percent-limit 0.5 \
    > "$OUT/perf_report_${IMPL}_flat.txt" 2>/dev/null
  echo "wrote $OUT/perf_report_$IMPL.txt and $OUT/perf_report_${IMPL}_flat.txt"
done

echo "=== perf stat: workload generation only (baseline) ==="
perf stat -r 5 -e "$EVENTS" ./bench --gen-only 2>&1 | tee "$OUT/perf_stat_gen.txt"
