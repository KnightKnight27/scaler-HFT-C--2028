#!/usr/bin/env bash
set -euo pipefail
submission_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$submission_dir" -B "$submission_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$submission_dir/build" -j 2
python3 "$submission_dir/scripts/check_float_rejection.py"
"$submission_dir/build/bench" --verify
"$submission_dir/build/bench" --verify --ops 100000 --seed 123456
cmake -S "$submission_dir" -B "$submission_dir/build-debug" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$submission_dir/build-debug" -j 2
"$submission_dir/build-debug/bench" --verify --ops 10000
cmake -S "$submission_dir" -B "$submission_dir/build-san" -DCMAKE_BUILD_TYPE=Release -DSANITIZE=ON
cmake --build "$submission_dir/build-san" -j 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$submission_dir/build-san/bench" --verify --ops 10000
