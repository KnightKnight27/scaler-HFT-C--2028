#!/usr/bin/env bash
# runs perf stat + perf record for A, B, C, then the timed benchmark. run on linux.
# perf runs use --notime so the profile shows the book and not clock_gettime.
set -u
cd "$(dirname "$0")/.."
mkdir -p perf results/linux

g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -o bench bench.cpp workload.cpp || exit 1

{ echo "uname: $(uname -srm)"; grep -m1 -iE "model name|^CPU implementer" /proc/cpuinfo; echo "cpus: $(nproc)";
  grep PRETTY_NAME /etc/os-release; g++ --version | head -1; $PERF --version;
  echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid)"; } > perf/machine_info.txt

PERF=${PERF:-perf}
EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults

for X in A B C; do
    echo "== perf stat $X =="
    $PERF stat -r 5 -e $EVENTS ./bench --impl $X --runs 1 --notime 2> perf/stat_$X.txt > /dev/null
    cat perf/stat_$X.txt
    echo "== perf record $X =="
    $PERF record -g -o perf/record_$X.data ./bench --impl $X --runs 1 --notime > /dev/null 2> perf/record_$X.log
    $PERF report -i perf/record_$X.data --stdio --no-children 2> /dev/null | head -80 > perf/report_$X.txt
    $PERF report -i perf/record_$X.data --stdio --no-children -g none 2> /dev/null | grep -v "^$" | head -40 > perf/report_${X}_flat.txt
done

# the timing numbers for the report come from here (plain -O2 build, 5 runs)
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -o bench bench.cpp workload.cpp || exit 1
for X in A B C; do
    ./bench --impl $X --runs 5 --json > results/linux/bench_$X.json
    ./bench --impl $X --runs 5 > results/linux/bench_$X.txt
done
python3 scripts/make_table.py > results/table.md
cat results/table.md
