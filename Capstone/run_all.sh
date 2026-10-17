#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"
mkdir -p build results

if command -v clang++ >/dev/null 2>&1; then
    CXX=clang++
else
    CXX=g++
fi

if ! "$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
    order_book.cpp ordered_map_order_book.cpp sorted_vector_order_book.cpp \
    fixed_array_order_book.cpp bench.cpp -o build/bench; then
    echo "C++ build failed." >&2
    exit 1
fi

if ! ./build/bench --verify > results/verify.txt 2>&1; then
    cat results/verify.txt
    echo "Correctness verification failed." >&2
    exit 1
fi

for impl in A B C; do
    if ! ./build/bench --impl "$impl" > "results/bench_${impl}.txt" 2>&1; then
        cat "results/bench_${impl}.txt"
        echo "Benchmark ${impl} failed." >&2
        exit 1
    fi
done

if [[ "$(uname -s)" == "Darwin" ]]; then
    if ! xcode-select -p >/dev/null 2>&1 ||
       [[ "$(xcode-select -p)" == "/Library/Developer/CommandLineTools" ]] ||
       ! command -v xctrace >/dev/null 2>&1; then
        echo "CPU Counters require a full Xcode installation and an active Xcode developer directory." >&2
        echo "Install Xcode, launch it once, then run: sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer" >&2
        exit 1
    fi

    {
        echo "macOS perf-mode benchmark results"
        echo "================================="
        echo "Workload baseline: ./build/bench --impl none --perf"
        echo "Implementations: ./build/bench --impl A|B|C --perf"
        echo "Counters: xctrace CPU Counters; IPC context: /usr/bin/time -l"
        echo
    } > results/perf_results.txt

    for impl in none A B C; do
        /usr/bin/time -l ./build/bench --impl "$impl" --perf \
            > "results/perf_run_${impl}.txt" 2> "results/time_${impl}.txt"
        {
            echo "============================================================"
            echo "Implementation ${impl}: --perf and /usr/bin/time -l"
            echo "============================================================"
            cat "results/perf_run_${impl}.txt"
            cat "results/time_${impl}.txt"
            echo
        } >> results/perf_results.txt
    done

    for impl in none A B C; do
        trace="results/xctrace_${impl}.trace"
        rm -rf "$trace"
        if ! xctrace record --template 'CPU Counters' \
                --output "$trace" --launch -- ./build/bench --impl "$impl" \
                --perf > "results/xctrace_run_${impl}.txt" 2>&1; then
            cat "results/xctrace_run_${impl}.txt" >&2
            echo "xctrace CPU Counters collection failed for implementation ${impl}." >&2
            exit 1
        fi

        if ! xctrace export "$trace" --toc \
                --output "results/perf_stat_${impl}.txt" \
                > "results/perf_report_${impl}.txt" 2>&1; then
            echo "xctrace export failed for implementation ${impl}; trace retained at ${trace}." \
                >> "results/perf_report_${impl}.txt"
            exit 1
        fi
        if ! xctrace export "$trace" \
                --xpath '/trace-toc/run/data/table[@schema="MetricTable"]' \
                --output "results/perf_report_${impl}.txt" \
                >/dev/null 2>&1; then
            echo "xctrace counter export failed for implementation ${impl}; trace retained at ${trace}." \
                >> "results/perf_report_${impl}.txt"
            exit 1
        fi
        {
            echo "============================================================"
            echo "Implementation ${impl}: xctrace MetricTable"
            echo "============================================================"
            cat "results/perf_report_${impl}.txt"
            echo
        } >> results/perf_results.txt
    done
    echo "Benchmarks and xctrace CPU Counters completed."
    exit 0
fi

if ! command -v perf >/dev/null 2>&1; then
    {
        echo "Linux perf results"
        echo "=================="
        echo "perf is not installed; counters and call-graph reports were not collected."
        echo "Install the Linux perf package and rerun ./run_all.sh."
    } > results/perf_results.txt
    for impl in A B C; do
        printf 'perf is not installed; counters were not collected.\n' \
            > "results/perf_stat_${impl}.txt"
        printf 'perf is not installed; call-graph report was not collected.\n' \
            > "results/perf_report_${impl}.txt"
    done
    echo "Benchmarks completed. Linux perf is unavailable; see results/perf_* files."
    exit 0
fi

EVENTS="task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"
{
    echo "Linux perf results"
    echo "=================="
    echo "Command: perf stat -r 5 -e $EVENTS ./build/bench --impl A|B|C"
    echo
} > results/perf_results.txt

for impl in none A B C; do
    if ! perf stat -r 5 -e "$EVENTS" -o "results/perf_stat_${impl}.txt" -- \
            ./build/bench --impl "$impl" --perf > "results/perf_run_${impl}.txt" 2>&1; then
        echo "perf stat did not complete successfully; inspect the recorded output and host permissions." \
            >> "results/perf_stat_${impl}.txt"
    fi

    if perf record -q -g -o "results/perf_${impl}.data" -- \
        ./build/bench --impl "$impl" --perf > "results/perf_run_${impl}.txt" 2>&1; then
        if ! perf report --stdio --no-children -i "results/perf_${impl}.data" \
            > "results/perf_report_${impl}.txt" 2>&1; then
            echo "perf report failed; inspect kernel permissions and perf.data." \
                >> "results/perf_report_${impl}.txt"
        fi
    else
        printf 'perf record did not complete; inspect results/perf_run_%s.txt and host permissions.\n' \
            "$impl" > "results/perf_report_${impl}.txt"
    fi

    {
        echo "============================================================"
        echo "Implementation ${impl}: perf stat"
        echo "============================================================"
        cat "results/perf_stat_${impl}.txt"
        echo
        echo "============================================================"
        echo "Implementation ${impl}: perf report"
        echo "============================================================"
        cat "results/perf_report_${impl}.txt"
        echo
    } >> results/perf_results.txt
done

echo "Benchmarks completed. Combined Linux perf output: results/perf_results.txt"
