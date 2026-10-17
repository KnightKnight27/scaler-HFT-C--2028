# Capstone: Low-Latency Order Book

Name: Anika Tripathi
SST Email: anika.24bcs10409@sst.scaler.com
Roll Number: 10409

## Overview

A single-instrument resting order book (no matching) implemented three ways in C++17:

| Impl | File | Price levels | Orders | Best price |
|---|---|---|---|---|
| A | `main_ob/book_map.hpp` | `std::map` (bids with `std::greater`) | `std::unordered_map<id, Order>` | `begin()` of each map |
| B | `main_ob/book_vec.hpp` | sorted `std::vector<Level>`, best at `back()` | `std::unordered_map<id, Order>` | `back()` of each vector |
| C | `main_ob/book_array.hpp` | flat `std::array<Qty, 10001>` indexed by tick | `std::vector<Order>` indexed by order id | cached best bid/ask, rescanned when the best level empties |

All three are driven by one benchmark binary (`bench/bench.cpp`) through templates, so there is no virtual call overhead and every implementation runs the exact same workload.

Design details, complexity, results and analysis are in `docs/REPORT.md`. Design decisions are in `NOTES.md`.

## Repository layout

```
Capstone/
  main_ob/types.hpp       shared types, tick constants, to_tick / to_price
  main_ob/book_map.hpp    Implementation A
  main_ob/book_vec.hpp    Implementation B
  main_ob/book_array.hpp  Implementation C
  bench/workload.hpp      deterministic workload generator (seed 42)
  bench/bench.cpp         benchmark + --verify mode
  results/                all raw outputs used in the report
  docs/REPORT.md          results table and performance analysis
  NOTES.md                design decisions
```

## Interface changes

Prices are integer ticks instead of `double`:

- `add_order(uint64_t id, Side side, Tick price, Qty qty)`, where `Tick` is `int32_t` and 1 tick = 0.01. For example, 100.01 is 10001.
- `Level` and `BBO` store `Tick` prices and `int64_t` quantities.
- `to_tick(double)` (uses `std::llround`) and `to_price(Tick)` in `types.hpp` convert at the edges. The workload generates ticks directly, and the benchmark prints prices as doubles.

Reason: exact integer comparisons make `--verify` reliable, avoid rounding bugs such as `0.29 * 100 = 28.999...`, and let Implementation C index an array directly by price.

Everything else follows the spec: `cancel_order` returns `false` for unknown or already-cancelled ids, an empty book reports `has_bid == has_ask == false`, and `get_bids/get_asks(depth)` return only the levels that exist.

## Build

Option 1, g++ (this is what the results were produced with):

```bash
mkdir -p build
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -I main_ob -I bench bench/bench.cpp -o build/bench
```

Option 2, CMake (defaults to RelWithDebInfo, which is `-O2 -g`):

```bash
mkdir -p build && cd build
cmake .. && make
cd ..
```

## Correctness test

```bash
./build/bench --verify
```

This runs explicit edge-case tests on each implementation (empty book, unknown cancel, double cancel, insufficient depth, best level emptying, same-price aggregation). It then runs the full 1,000,000-op benchmark workload through every pair (A vs B, A vs C, B vs C) and compares the BBO, top-10 bids, top-10 asks and cancel return values after every operation. Output is in `results/verify.txt`:

```
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## Benchmark

```bash
./build/bench --impl A --runs 5
./build/bench --impl B --runs 5
./build/bench --impl C --runs 5
```

Each run executes 1,000,000 operations (about 70% add, 20% cancel of a live order, 10% BBO, plus `get_bids(10)` and `get_asks(10)` every 100 ops). Each op type is timed separately with `std::chrono::steady_clock`, and the median of 5 runs is printed together with the final BBO and a checksum so the work can't be optimised away. Options: `--runs N`, `--seed S`.

## Profiling

```bash
sudo sysctl -w kernel.perf_event_paranoid=-1   # needed in Codespaces
sudo sysctl -w kernel.kptr_restrict=0          # kernel symbols in perf report

perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./build/bench --impl A
perf record -g -o perf_A.data ./build/bench --impl A --runs 1
perf report -i perf_A.data --stdio --no-children --sort symbol
```

Outputs are saved in `results/` as `perf_stat_*.txt`, `perf_report_*.txt`, `pagefault_experiment.txt` and `memory_usage.txt`.

## Environment

All results in `results/` and `docs/REPORT.md` come from:

- GitHub Codespaces, x86_64, 2 vCPU, Linux 6.8.0-1064-azure
- g++ 13.3.0, `-O2`

Hardware PMU counters (cycles, instructions, branches, cache misses, dTLB) show `<not supported>` on this VM. task-clock, page-faults and `perf record` work.

Note: `std::uniform_int_distribution` is implemented differently in libstdc++ (Linux) and libc++ (macOS), so seed 42 produces a different workload, and therefore a different checksum, on macOS. All reported numbers come from the Linux run above (checksum 483587156).