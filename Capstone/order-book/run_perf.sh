mkdir -p results
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -o bench benchmark/bench.cpp && g++ -std=c++17 -O2 -o verify tests/verify.cpp || exit 1

./verify | tee results/verify.txt
for I in A B C; do ./bench --impl $I | tee results/bench_$I.txt; done

EVENTS=task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults
for I in A B C; do
  perf stat -r 5 -e $EVENTS ./bench --impl $I --runs 1 --no-op-timing 2>&1 | tee results/perf_stat_$I.txt
  perf record -g -o results/perf_$I.data ./bench --impl $I --runs 1 --no-op-timing > /dev/null
  perf report -i results/perf_$I.data --stdio --no-children 2>/dev/null | head -60 > results/perf_report_$I.txt
done