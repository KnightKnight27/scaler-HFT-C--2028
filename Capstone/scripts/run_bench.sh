#!/usr/bin/env bash
# run_bench.sh — benchmark A, B and C with every run in a FRESH process.
#
#   scripts/run_bench.sh            5 processes per impl (default)
#   scripts/run_bench.sh 7          7 processes per impl
#
#   TAG=batch     BENCH_ARGS="--batch"           scripts/run_bench.sh   block-timed per-op costs
#   TAG=scrambled BENCH_ARGS="--ids scrambled"   scripts/run_bench.sh   NON-GRADED variant
#   TAG=thin      BENCH_ARGS="--mix thin"        scripts/run_bench.sh   NON-GRADED variant
#
# Why separate processes: runs inside one process share heap state. A and B allocate and
# free hundreds of thousands of nodes per run, so later in-process runs can be affected by
# earlier ones. Each process here runs `bench --impl X --runs 1 $BENCH_ARGS`, and the script
# reports the median of the per-process numbers.
#
# Raw output goes to results/bench_[TAG_]{A,B,C}.txt (one block per process) and the medians
# to results/bench_summary[_TAG].txt. These files are for the authors to keep: numbers count
# only if they were produced on the machine the report describes.
set -euo pipefail

cd "$(dirname "$0")/.."
runs="${1:-5}"
out="${RESULTS_DIR:-results}"  # override to keep trial runs out of results/
tag="${TAG:-}"
read -r -a extra_args <<< "${BENCH_ARGS:-}"
bench=./build/bench
[[ -x "$bench" || -x "$bench.exe" ]] || { echo "build first: scripts/build.sh" >&2; exit 1; }

mkdir -p "$out"
summary="$out/bench_summary${tag:+_$tag}.txt"
{
    echo "# bench summary: $runs fresh processes per impl, median of each metric"
    echo "# args: --runs 1 ${BENCH_ARGS:-}"
    echo "# host: $(uname -srm)  date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# $("$bench" --impl A --runs 1 --total-only --ops 1000 | sed -n 's/^Build: /build: /p')"
} > "$summary"

# median of the numbers on stdin
median() { sort -g | awk '{ v[NR] = $1 } END { if (NR % 2) print v[(NR + 1) / 2]; else print (v[NR / 2] + v[NR / 2 + 1]) / 2 }'; }

for impl in A B C; do
    raw="$out/bench_${tag:+${tag}_}${impl}.txt"
    : > "$raw"
    for ((r = 1; r <= runs; r++)); do
        echo "=== process $r/$runs ===" >> "$raw"
        "$bench" --impl "$impl" --runs 1 "${extra_args[@]}" >> "$raw"
    done

    # Each process prints "Run 1: total X ms | ns/add A ns/cancel B ns/bbo C ns/depth D",
    # and with --batch also "| R2 R | perturbed P ms".
    total=$(sed -n 's/^Run 1: total *\([0-9.]*\) ms.*/\1/p' "$raw" | median)
    metric() { sed -n "/^Run 1:/s/.*ns\/$1 *\([-0-9.]*\).*/\1/p" "$raw" | median; }
    checksums=$(sed -n 's/^Checksum: //p' "$raw" | sort -u | tr '\n' ' ')
    batch_part=""
    if grep -q "| R2 " "$raw"; then
        r2=$(sed -n 's/^Run 1:.*| R2 *\([-0-9.]*\) .*/\1/p' "$raw" | median)
        perturbed=$(sed -n 's/^Run 1:.*| perturbed *\([0-9.]*\) ms.*/\1/p' "$raw" | median)
        batch_part=" | R2 $r2 | perturbed $perturbed ms"
    fi

    printf 'Impl %s: total %s ms | ns/add %s | ns/cancel %s | ns/bbo %s | ns/depth %s%s | checksum(s) %s\n' \
        "$impl" "$total" "$(metric add)" "$(metric cancel)" "$(metric bbo)" "$(metric depth)" "$batch_part" \
        "$checksums" | tee -a "$summary"
done

echo "Raw output: $out/bench_${tag:+${tag}_}{A,B,C}.txt   Summary: $summary"
echo "Within one workload, every impl must show the same single checksum."
