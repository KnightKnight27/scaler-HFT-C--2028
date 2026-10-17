#!/bin/bash
# real runs on the machine I am submitting from. not a fancy framework.
# run: cd Capstone/10447 && ./experiments/run_current_machine.sh
set -u

out=results/current_machine
mkdir -p "$out"

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -o bench bench.cpp || exit 1

{
    date -Is
    uname -a
    echo
    lscpu | grep -E 'Model name|CPU\(s\)|Thread|Core\(s\)|CPU max MHz|L[123].*cache'
    echo
    free -h
    echo
    g++ --version | head -2
    echo "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo unavailable)"
} > "$out/system.txt"

./bench --verify > "$out/verify.txt"

# Small and medium are just a sanity/scaling check. 1M is the actual assignment size.
for n in 10000 100000 1000000; do
    for i in A B C; do
        ./bench --impl "$i" --ops "$n" --runs 3 > "$out/${i}_${n}.txt"
    done
done

# It should fail instead of quietly choosing some book. This is expected to return 1.
./bench --impl definitely-not-a-book > "$out/expected_bad_cli.txt" 2>&1
echo "exit=$? (expected 1)" >> "$out/expected_bad_cli.txt"

# No sudo here. It is useful if the machine allows it, harmless if it does not.
perf stat -e task-clock ./bench --impl C --perf > /dev/null 2> "$out/perf_smoke.txt" || true

echo "done: look in $out"
