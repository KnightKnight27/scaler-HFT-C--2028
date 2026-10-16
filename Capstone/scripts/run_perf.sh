#!/usr/bin/env bash
# run_perf.sh — perf stat + perf record for implementations A, B and C. Linux only.
#
#   scripts/run_perf.sh                  everything, 5 repeats per perf stat
#   PERF_REPEATS=10 scripts/run_perf.sh  more repeats
#   PERF=/usr/lib/linux-tools/<ver>-generic/perf scripts/run_perf.sh   use a specific perf binary
#                                        (needed on WSL: the /usr/bin/perf wrapper rejects its kernel)
#   RESULTS_DIR=/tmp/x scripts/run_perf.sh   write somewhere other than results/
#
# This is meant to be run by the authors on the machine the report describes. Its output
# files are the raw evidence for the results table; never edit or fill them in by hand.
#
# What is measured:
#   perf stat -r N  ./build/bench --impl X --runs 1 --total-only
#       One process = workload generation + ONE total-time pass (no per-op clock calls,
#       which would add ~2M clock_gettime calls to the counters). perf's -r N is the
#       repetition mechanism; it reports the mean and the variation across the N processes.
#   perf stat -r N  ./build/bench --generate-only            (the "baseline")
#       Process start + workload generation only, no book. Compare it with A/B/C to see
#       how much of each counter (especially page-faults and instructions) is the
#       generator, not the book.
#   perf record --call-graph fp  ./build/bench_perf --impl X --runs 1 --total-only
#       bench_perf = same -O2 -DNDEBUG build plus -g -fno-omit-frame-pointer, so
#       call stacks can be walked. Hot spots are written to perf_report_X*.txt.
#
# Output (in results/ unless RESULTS_DIR is set):
#   perf_env.txt                 machine, kernel, CPU, perf version, compiler, paranoid level
#   perf_stat_{baseline,A,B,C}.txt   raw perf stat output
#   perf_stat_*_stdout.txt       the program's own output during perf stat
#   perf_unsupported.txt         every counter perf reported as <not supported>/<not counted>
#   perf_summary.md              table built from the raw files by scripts/perf_summary.sh
#   perf_report_{A,B,C}.txt      perf report, flat (self time) hot spots
#   perf_report_{A,B,C}_callgraph.txt   perf report with caller chains
#   perf.data.{A,B,C}            raw samples (git-ignored; keep locally). On WSL with a
#                                results dir under /mnt they go to ~/orderbook-perf-data
#                                instead (override with PERF_DATA_DIR).
set -euo pipefail

cd "$(dirname "$0")/.."
out="${RESULTS_DIR:-results}"
reps="${PERF_REPEATS:-5}"
perf_bin="${PERF:-perf}"
events="task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"

# --- preflight ------------------------------------------------------------------

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "perf needs Linux (this is $(uname -s)). Run this on the Linux/lab machine." >&2
    exit 1
fi
if ! command -v "$perf_bin" >/dev/null 2>&1; then
    echo "perf not found. Install it, e.g. 'sudo apt install linux-tools-common linux-tools-\$(uname -r)'" >&2
    echo "(Debian: linux-perf; Fedora: perf)." >&2
    exit 1
fi

paranoid="$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo unknown)"
echo "kernel.perf_event_paranoid = $paranoid"
if [[ "$paranoid" =~ ^[0-9]+$ ]] && (( paranoid > 2 )); then
    echo "WARNING: perf_event_paranoid > 2 blocks perf for normal users." >&2
    echo "  Lower it (until reboot): sudo sysctl kernel.perf_event_paranoid=2" >&2
fi
perf_data_dir="${PERF_DATA_DIR:-$out}"
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "WARNING: this looks like WSL. Most hardware counters will be <not supported>;" >&2
    echo "  the results will show which ones. A native Linux machine is preferable." >&2
    # perf record writes perf.data through mmap, which fails on Windows drives mounted
    # under /mnt ("failed to write perf data, error: Bad address"). Use the Linux filesystem.
    if [[ -z "${PERF_DATA_DIR:-}" && "$(realpath -m "$out")" == /mnt/* ]]; then
        perf_data_dir="$HOME/orderbook-perf-data"
        echo "  perf.data files go to $perf_data_dir (Windows drives can't hold them)." >&2
    fi
fi

# --- build ------------------------------------------------------------------------

scripts/build.sh release >/dev/null
scripts/build.sh perf >/dev/null
mkdir -p "$out" "$perf_data_dir"

{
    echo "date:     $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host:     $(uname -a)"
    echo "cpu:      $(lscpu 2>/dev/null | sed -n 's/^Model name: *//p' | head -1)"
    echo "cores:    $(nproc)"
    echo "perf:     $("$perf_bin" --version)"
    echo "paranoid: $paranoid"
    echo "compiler: $(g++ --version | head -1)"
    echo "bench:    $(./build/bench --impl A --runs 1 --total-only --ops 1000 | sed -n 's/^Build: //p')"
    echo "flags:    build/bench = -std=c++17 -O2 -DNDEBUG; build/bench_perf adds -g -fno-omit-frame-pointer"
    echo "repeats:  perf stat -r $reps"
} > "$out/perf_env.txt"
cat "$out/perf_env.txt"

# --- perf stat ------------------------------------------------------------------

for target in baseline A B C; do
    if [[ "$target" == baseline ]]; then
        cmd=(./build/bench --generate-only)
    else
        cmd=(./build/bench --impl "$target" --runs 1 --total-only)
    fi
    echo "perf stat: $target (${reps} runs)"
    "$perf_bin" stat -r "$reps" -e "$events" -o "$out/perf_stat_${target}.txt" -- "${cmd[@]}" \
        > "$out/perf_stat_${target}_stdout.txt"
done

if grep -H -E "<not (supported|counted)>" "$out"/perf_stat_{baseline,A,B,C}.txt > "$out/perf_unsupported.txt"; then
    echo "Some counters were unavailable (see $out/perf_unsupported.txt):"
    cat "$out/perf_unsupported.txt"
else
    echo "All requested counters were supported." > "$out/perf_unsupported.txt"
fi

# --- perf record ----------------------------------------------------------------

for impl in A B C; do
    echo "perf record: $impl"
    "$perf_bin" record --call-graph fp -o "$perf_data_dir/perf.data.$impl" -- \
        ./build/bench_perf --impl "$impl" --runs 1 --total-only > /dev/null
    "$perf_bin" report -i "$perf_data_dir/perf.data.$impl" --stdio --no-children -g none --percent-limit 0.5 \
        > "$out/perf_report_${impl}.txt" 2>/dev/null
    "$perf_bin" report -i "$perf_data_dir/perf.data.$impl" --stdio --no-children -g caller --percent-limit 2 \
        > "$out/perf_report_${impl}_callgraph.txt" 2>/dev/null
done

# --- summary --------------------------------------------------------------------

scripts/perf_summary.sh "$out" | tee "$out/perf_summary.md"
echo
echo "Done. Raw files are in $out/. Workload generation shows up in the profiles too"
echo "(generate_workload, mt19937): attribute it to the generator, not the book."
