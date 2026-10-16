# Capstone: Low-Latency Order Book (C++17)

**Name:** `Payel Manna`  
**Roll no.:** `24BCS10400` 
**SST email:** `payel.24bcs10400@sst.scaler.com`   

## Folder structure
```
order-book/
├── src/                     # the order books (one file each) + shared pieces
│   ├── common.hpp           # Side, Level, BBO, price<->tick helpers
│   ├── order_book_a.hpp     # Implementation A: std::map + std::unordered_map
│   ├── order_book_b.hpp     # Implementation B: sorted vector + std::map
│   ├── order_book_c.hpp     # Implementation C: price array + order vector by id
│   └── workload.hpp         # deterministic random workload (seed 42)
├── benchmark/
│   └── bench.cpp            # ONE benchmark program: ./bench --impl A|B|C
├── tests/
│   └── verify.cpp           # correctness test: A, B, C must agree
├── results/                 # verify / bench / perf output
├── run_perf.sh              # builds + verifies + benchmarks + perf for A, B, C
├── REPORT.md
└── README.md
```

## Build
Run from the `order-book/` folder (headers are included by relative path, so no extra flags are needed):
```bash
g++ -std=c++17 -O2 -o bench  benchmark/bench.cpp
g++ -std=c++17 -O2 -o verify tests/verify.cpp
```
Standard library only. For readable `perf record -g` call graphs you may add `-g -fno-omit-frame-pointer` (still `-O2`).

## Correctness test
```bash
./verify
```
Expected:
```
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```
It first runs directed tests (empty book, cancel unknown / cancel twice, same-price aggregation, insufficient depth, invalid
prices, duplicate ids), then runs the same 1,000,000-op random workload through A, B and C and compares every cancel result,
every BBO, `get_bids/get_asks(10)` every 100 ops, and the full bid/ask ladders every 10,000 ops.

## Benchmarks
```bash
./bench --impl A      # then B, then C
```
* Same workload for all: 1,000,000 ops, `std::mt19937 rng(42)`, ~70% add / 20% cancel (always a live order) / 10% BBO,
  `get_bids(10)` + `get_asks(10)` after every 100th op, qty 1–100, random valid tick on the correct side.
* 5 runs, median reported. Each run has (1) a pass timing the whole loop (**total time**) and (2) a pass timing every call with
  `std::chrono::steady_clock` (**ns/add, ns/cancel, ns/get_bbo**; the cost of two clock reads, ~20–30 ns, is subtracted).
* Prints final BBO and a checksum so the compiler cannot remove the work.
* Options: `--runs N`, `--ops N`, `--no-op-timing`.

## perf
```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A --runs 1 --no-op-timing
perf record -g ./bench --impl A --runs 1 --no-op-timing && perf report
```
(Same for B and C; `./run_perf.sh` does all of it.) `--runs 1 --no-op-timing` makes perf measure only the plain workload loop.

On my laptop `kernel.perf_event_paranoid` defaulted to 4 (no counters); I ran `sudo sysctl -w kernel.perf_event_paranoid=-1` and then all nine
counters were available. Output is in `results/perf_stat_A.txt`, `perf_stat_B.txt`, `perf_stat_C.txt`; `perf record -g` / `perf report` output is in
`results/perf_report_*.txt`. Page faults / peak memory were also checked with `/usr/bin/time -v`.

## Results summary (1,000,000 ops)
| | A | B | C |
|---|---|---|---|
| Total time (ms) | 598.75 | 772.99 | 90.86 |
| ns / add | 560.2 | 573.4 | 89.6 |
| ns / cancel | 932.8 | 1793.2 | 170.7 |
| ns / get_bbo | 21.2 | 23.6 | 14.1 |
| IPC | 0.82 | 0.86 | 1.51 |
| Branch misses (%) | 5.43 | 5.54 | 2.13 |
| L1-dcache-load-misses | 21,892,632 | 28,402,626 | 5,363,202 |
| Cache misses | 7,149,724 | 10,006,422 | 2,797,925 |
| Page faults | 23,538 | 24,012 | 24,325 |

Full table, correctness output and analysis: `REPORT.md`.

## Interface changes
* Classes are `OrderBookA`, `OrderBookB`, `OrderBookC` with the same five methods and signatures as the assignment.
* Prices stay `double` in the API but are converted to **integer ticks** (`llround(price * 100)`) inside; results convert back.
* Invalid orders (price outside the side's band, qty <= 0) and duplicate order ids are silently ignored.

## Data-structure choices
| | A | B | C (own design) |
|---|---|---|---|
| Price levels | `std::map` | sorted `std::vector`, best at the back | plain array indexed by tick |
| Orders | `std::unordered_map` (hash) | `std::map` (tree) | `std::vector` indexed by order id |
| Best price | `begin()` | `back()` | cached best tick, rescanned only when best level empties |
| Cancel lookup | hash by id | tree by id | direct array index by id |

C assumes small order ids (ids >= 2^24 are rejected); the benchmark uses ids 1, 2, 3, ...