#!/usr/bin/env bash
# build.sh — build the binaries with plain g++ (no CMake needed).
#
#   scripts/build.sh            release: -O2 -DNDEBUG                -> build/bench        (graded numbers)
#   scripts/build.sh perf       release + -g -fno-omit-frame-pointer -> build/bench_perf   (perf record -g)
#   scripts/build.sh debug      -O0 -g, invariant asserts on         -> build/bench_debug
#   scripts/build.sh asan       -O1 -g + address/UB sanitizers       -> build/bench_asan   (Linux; not MinGW)
#
# Each mode also builds the edge tests (build/edge_tests, build/edge_tests_<mode>).
# Every mode has its own output name, so a debug or sanitizer build can never be timed by
# mistake in place of build/bench.
set -euo pipefail

cd "$(dirname "$0")/.."
mode="${1:-release}"

common=(-std=c++17 -Wall -Wextra -Wpedantic -Iinclude -Isrc)
case "$mode" in
    release) flags=(-O2 -DNDEBUG);                                     suffix="" ;;
    perf)    flags=(-O2 -DNDEBUG -g -fno-omit-frame-pointer);          suffix="_perf" ;;
    debug)   flags=(-O0 -g);                                           suffix="_debug" ;;
    asan)    flags=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined); suffix="_asan" ;;
    *) echo "unknown mode: $mode (release|perf|debug|asan)" >&2; exit 2 ;;
esac

# Tests always keep assert() enabled, whatever the mode: drop -DNDEBUG.
test_flags=()
for f in "${flags[@]}"; do [[ "$f" == "-DNDEBUG" ]] || test_flags+=("$f"); done

mkdir -p build
set -x
g++ "${common[@]}" "${flags[@]}" src/bench.cpp src/workload.cpp src/verify.cpp -o "build/bench${suffix}"
g++ "${common[@]}" "${test_flags[@]}" tests/edge_tests.cpp -o "build/edge_tests${suffix}"
