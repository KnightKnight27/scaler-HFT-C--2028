#!/usr/bin/env bash
# perf_summary.sh [DIR] — build a Markdown table from DIR/perf_stat_{baseline,A,B,C}.txt
# (default DIR = results). Only reads what perf wrote: a counter perf did not report is
# shown as "n/s" (not supported) or "-" (absent), never estimated.
#
# Handles: thousands separators, "msec task-clock", ":u"/":k" event suffixes (restricted
# perf_event_paranoid), and hybrid Intel CPUs where perf splits an event into
# cpu_core/<event>/ and cpu_atom/<event>/ lines (the two are summed, and this is noted).
set -euo pipefail

dir="${1:-results}"
targets=(baseline A B C)
events=(task-clock cycles instructions branches branch-misses cache-references cache-misses
        L1-dcache-load-misses dTLB-load-misses page-faults)

# value FILE EVENT -> number | n/s | -
value() {
    [[ -f "$1" ]] || { echo "-"; return; }
    awk -v ev="$2" '
        {
            for (j = 1; j <= NF && $j != "#"; j++) {   # ignore the "# ..." comment part
                name = $j
                sub(/^cpu_(core|atom)\//, "", name)       # hybrid CPU prefix
                sub(/\/[ukhG]*$/, "", name)                # hybrid suffix: cycles/u
                sub(/:[ukhG]+$/, "", name)                 # cycles:u
                if (name != ev) continue
                if ($0 ~ /<not (supported|counted)>/) { unsupported = 1; break }
                v = $(j - 1)
                if (v == "msec") v = $(j - 2)
                gsub(/,/, "", v)
                total += v; found = 1; parts++
                break
            }
        }
        END {
            if (found) printf (ev == "task-clock" ? "%.2f\n" : "%.0f\n"), total
            else if (unsupported) print "n/s"
            else print "-"
        }' "$1"
}

hybrid_note=""
declare -A v
for t in "${targets[@]}"; do
    f="$dir/perf_stat_${t}.txt"
    for e in "${events[@]}"; do v[$t,$e]="$(value "$f" "$e")"; done
    if [[ -f "$f" ]] && grep -q "cpu_atom/" "$f"; then hybrid_note=1; fi
done

# ratio NUM DEN SCALE DECIMALS -> formatted ratio or n/a
ratio() {
    awk -v n="$1" -v d="$2" -v s="$3" -v p="$4" 'BEGIN {
        if (n !~ /^[0-9.]+$/ || d !~ /^[0-9.]+$/ || d == 0) { print "n/a"; exit }
        printf "%." p "f\n", n / d * s }'
}

echo "# perf stat summary"
echo
echo "Source: \`$dir/perf_stat_*.txt\` (means over the perf stat -r repeats)."
echo "baseline = \`bench --generate-only\` (process start + workload generation, no book)."
echo "n/s = not supported on this machine; - = not present in the file."
[[ -n "$hybrid_note" ]] && echo "Hybrid CPU: cpu_core and cpu_atom counts were summed per event."
echo
echo "| Counter | baseline | A | B | C |"
echo "|---|---|---|---|---|"
for e in "${events[@]}"; do
    label="$e"; [[ "$e" == task-clock ]] && label="task-clock (ms)"
    echo "| $label | ${v[baseline,$e]} | ${v[A,$e]} | ${v[B,$e]} | ${v[C,$e]} |"
done
echo
echo "| Derived | Formula | baseline | A | B | C |"
echo "|---|---|---|---|---|---|"
row() {  # label formula num-event den-event scale decimals
    printf '| %s | %s |' "$1" "$2"
    for t in "${targets[@]}"; do printf ' %s |' "$(ratio "${v[$t,$3]}" "${v[$t,$4]}" "$5" "$6")"; done
    echo
}
row "IPC" "instructions / cycles" instructions cycles 1 2
row "Branch-miss %" "branch-misses / branches × 100" branch-misses branches 100 2
row "Cache-miss %" "cache-misses / cache-references × 100" cache-misses cache-references 100 2
