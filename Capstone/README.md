# Low-Latency Order Book

**Team size:** 2

| Student | Roll number | SST email |
|---|---|---|
| Tanush Shoor | 24BCS10265 | tanush.24bcs10265@sst.scaler.com |
| Vivaan Goyal | 24BCS10163 | vivaan.24bcs10163@sst.scaler.com |

This C++17 project implements a single-instrument resting order book. It supports add, cancel, BBO, and depth queries, with price-time priority for orders at the same price. Orders remain in the book until cancelled; the program does not match orders or generate trades.

## Build

Run from this directory:

~~~sh
g++ -std=c++17 -O2 -Wall -Wextra -pedantic order_book.cpp implementations.cpp bench.cpp -o bench
~~~

## Run

~~~sh
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
~~~

The deterministic workload uses seed 42 and 1,000,000 operations: approximately 70% adds, 20% cancels, and 10% BBO queries. Every 100 operations, it also requests the top ten bids and asks. Cancels select currently live orders. The benchmark runs five trials internally and reports their median along with average nanoseconds per add, cancel, and BBO call. A final BBO and checksum prevent the workload from being optimized away.

## Interface and validation

The API is declared in order_book.hpp. Prices are passed as doubles and represented internally in integer cents. Buy prices must be in [50.00, 99.99], sell prices in [100.01, 150.00], and all prices must be exact 0.01 ticks. Quantities must be positive and order IDs must be unique. Invalid additions throw std::invalid_argument. Cancelling an unknown or already-cancelled ID returns false. An empty book returns a BBO with has_bid and has_ask both false.

## Implementations

| Design | Order and level storage | Best price | Cancel lookup |
|---|---|---|---|
| A | std::map keyed by tick; each level stores a FIFO std::list and aggregate quantity | Last bid-map entry and first ask-map entry | Hash map from ID to list iterator |
| B | Sorted vectors of aggregate price levels; hash map stores side, tick, and quantity per live order | Vector endpoints | Hash map lookup, then binary search for the level |
| C | Fixed arrays indexed by tick, order-ID hash map, and generation-checked priority queues | Lazy cleanup of stale entries, then heap tops | Hash lookup and direct array update |

Let L be active price levels and D the requested depth. Hash-map operations are expected O(1).

| Operation | A | B | C |
|---|---:|---:|---:|
| Add | O(log L) | O(L) when inserting a new level | Expected O(1) for an active level; O(log L) when activating a level and pushing it to a heap |
| Cancel | Expected O(1) | O(L) if a level is erased and vector entries shift | Expected O(1) |
| BBO | O(1) | O(1) | Amortized O(1), with stale heap entries removed over time |
| Bids / asks | O(D) | O(D) | O(10,001) fixed-range scan |

## Correctness

The --verify mode replays the same seeded workload through A, B, and C and compares BBO, top-ten levels, prices, quantities, and cancellation results after every operation. It also checks empty-book BBO and an unknown cancellation.

~~~text
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
~~~

## Final Linux benchmark results

The following table uses the median of the five benchmark summaries in the final supplied output for each implementation. Each summary itself reports the median of five internal benchmark trials; the five summaries were emitted during the five repetitions used by perf stat.

| Metric | A: map + FIFO | B: sorted vector | C: indexed array + heaps |
|---|---:|---:|---:|
| Total time (ms) | 326.448 | 210.859 | 248.618 |
| ns / add_order | 280.337 | 162.991 | 88.1522 |
| ns / cancel_order | 440.064 | 295.886 | 244.111 |
| ns / get_bbo | 54.3794 | 46.4098 | 50.8166 |

All implementations produced bid 99.99 x 2722, ask 100.01 x 2525, and checksum 4495818852. B had the lowest total time and BBO latency. C had the lowest add and cancel latency. See REPORT.md for perf counters, derived IPC and branch-miss percentage, and analysis.

## Linux profiling

Run these commands on Linux with perf installed and permitted:

~~~sh
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl B
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl C
perf record -o perf-A.data -g ./bench --impl A
perf report -i perf-A.data
perf record -o perf-B.data -g ./bench --impl B
perf report -i perf-B.data
perf record -o perf-C.data -g ./bench --impl C
perf report -i perf-C.data
~~~

The supplied final perf stat output includes all requested counters for A, B, and C. The measured values and event multiplexing details are recorded in REPORT.md. Raw terminal output in Markdown is sufficient to present the evidence; screenshots are optional unless the instructor specifically requests them.

