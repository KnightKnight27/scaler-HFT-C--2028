# Order Book Performance Report

## Test setup

The benchmark was Macbook-pro (M4-pro) 24GB RAM using
`-std=c++17 -O2 -Wall -Wextra -Werror -pedantic` and run natively on this
computer.
As , we dont have access to linux , we used docker linux lightweight VM and the results are below this instructions to run your own linux OS, as Mac restricted some benchmarks even on VM , we tried to get important benchmarks to analyze and documented them on this report.
Each implementation ran the same fixed-seed
1,000,000-operation workload five times. 
The results below are the medians of those five runs;
the complete run-by-run output is in `results/bench_A.txt`,
`results/bench_B.txt`, and `results/bench_C.txt`.

## Measured results

| Metric | A: ordered map | B: sorted vector | C: fixed tick array |
| :--- | :---: | :---: | :---: |
| Median total time (ms) | 98.6 | 82.6 | 48.9 |
| Average add (ns) | 70.1 | 55.3 | 25.3 |
| Median of run p99 add (ns) | 125 | 209 | 42 |
| Average cancel (ns) | 166.4 | 143.6 | 77.0 |
| Median of run p99 cancel (ns) | 417 | 375 | 292 |
| Average BBO (ns) | 13.2 | 12.9 | 12.8 |
| Median of run p99 BBO (ns) | 42 | 42 | 42 |

 ## xtrace results
 
| xtrace counter | A: ordered map | B: sorted vector | C: fixed tick array |
|---|---|---|---|
| Cycles | 513,532 | 393,478 | 549,054 |
| Instruction delivery bottleneck | 37.47% | 37.00% | 39.28% |
| Discarded bottleneck | 20.00% | 19.92% | 20.00% |
| Instruction processing bottleneck | 16.59% | 16.30% | 16.22% |
| Useful cycles | 25.09% | 25.96% | 23.60% |


## Linux perf commands and output

On Linux, install the compiler and `perf`, then run the complete workflow:

```sh
sudo apt update
sudo apt install build-essential linux-tools-common linux-tools-$(uname -r)
chmod +x run_all.sh
./run_all.sh
```

The script compiles the project, runs correctness verification, benchmarks
implementations A/B/C, and writes the complete combined `perf stat` and
`perf report` output to:

```text
results/perf_results.txt
```

The individual files `results/perf_stat_A.txt` through
`results/perf_stat_C.txt` and `results/perf_report_A.txt` through
`results/perf_report_C.txt` are also retained.

To run the Linux commands manually:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  order_book.cpp ordered_map_order_book.cpp sorted_vector_order_book.cpp \
  fixed_array_order_book.cpp bench.cpp -o build/bench
./build/bench --verify
EVENTS="task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"
perf stat -r 5 -e "$EVENTS" -- ./build/bench --impl A
perf record -q -g -o results/perf_A.data -- ./build/bench --impl A
perf report --stdio --no-children -i results/perf_A.data
```

Repeat the final three commands with `--impl B` and `--impl C` to collect
the other implementations. `run_all.sh` performs all three runs and combines
their complete output in `results/perf_results.txt`.

All three runs reported the same final BBO (bid 99.99 x 2826, ask 100.01 x
2307) and checksum (`5616446185434`). The differential correctness run passed
for all implementations; its actual output is in `results/verify.txt`.

CPU Counters were collected with Apple's Xcode Instruments `CPU Counters`
template. The complete xctrace table-of-contents output is stored in each
`perf_stat_*.txt` file, and the complete numeric `MetricTable` export is
stored in each `perf_report_*.txt` file. The values above are the measured
process-level sample from those exports.

The per-operation timer calibration printed a median empty interval of 0 ns
on this host. This can result from the clock's resolution and does **not**
mean the clock calls have zero overhead. Operation latency samples include
the clock calls and sample recording; p99 values are quantized at this
environment's timer resolution. Total runtime includes per-operation timing
and periodic depth reads. These are measured benchmark results, not estimates,
but should be compared on the same host and build configuration.

## Requested perf measurement method

Implementation counters use one untimed run:

```sh
./build/bench --impl X --perf
```

The `none` baseline measures only generation of the same one-million-operation
workload:

```sh
./build/bench --impl none --perf
```

Subtract the `none` counter values from A, B, and C. On macOS, Apple Clang
17 with `-std=c++17 -O2` runs directly on the Mac. xctrace CPU Counters
collects fixed cycles, fixed instructions, instruction branches, branch
mispredictions, L1D load misses, and L1D TLB misses. `/usr/bin/time -l`
provides IPC context and process resource values. The M3 has no last-level
cache event, so LLC values come only from Cachegrind simulation.

### Docker results

| Simulated metric | A: ordered map | B: sorted vector | C: fixed tick array |
|---|---:|---:|---:|
| Instruction references | 505,320,151 | 621,664,186 | 425,475,369 |
| I1 cache misses | 567 | 380 | 194 |
| Last-level instruction misses | 541 | 309 | 201 |
| Data references | 218,674,721 | 240,291,244 | 179,432,204 |
| D1 cache misses | 13,154,656 | 13,780,403 | 4,485,865 |
| Last-level data misses | 7,197,068 | 4,106,355 | 3,850,396 |
| Last-level cache misses | 7,197,609 | 4,106,664 | 3,850,597 |

For the Debian Docker comparison:

```sh
# Start Docker Desktop first, then run:
docker run --rm -it -v "$PWD:/src" -w /src debian:bookworm bash
apt-get update
apt-get install -y build-essential linux-perf valgrind
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer \
  order_book.cpp ordered_map_order_book.cpp sorted_vector_order_book.cpp \
  fixed_array_order_book.cpp bench.cpp -o build/bench-linux
./build/bench-linux --impl none --perf
perf stat -e task-clock,page-faults -- ./build/bench-linux --impl A --perf
valgrind --tool=cachegrind ./build/bench-linux --impl A --perf
```

Repeat the final two commands for B and C. Docker on Apple Silicon exposes
only task-clock and page-faults to `perf stat`; Cachegrind is simulated.
`run_all.sh` consolidates automated output in `results/perf_results.txt`.



## Analysis

Implementation C was fastest in this measurement for total time, add, and
cancel. Its price levels are stored in fixed arrays indexed by valid tick, so
an add updates one slot directly, and BBO reads return maintained best ticks.
The sorted-vector design B had the lowest median average BBO time by a small
margin (12.9 ns versus 13.2 ns for both A and C); this difference is too small to
interpret as a meaningful winner without more precise measurement.

The vector design's add p99 was highest at 209 ns. Cancelling an
order requires finding its price level and can erase a level, shifting later
vector elements. A's tree-backed levels have separate node allocations and
pointer-based traversal, which can raise average costs, but do not require
shifting all later levels on a price-level deletion. C's array lookup is
direct, though cancelling the last order at the current best price may scan
ticks to find the next level. Its observed p99 was lower on this workload, but
the fixed price bounds and workload are important to that result.
