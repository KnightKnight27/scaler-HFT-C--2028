# Low Latency Order Book (C++17)

**Student:** SST email: `tanush.24bcs10265@sst.scaler.com` · Roll number: `24BCS10265`

Single instrument, resting orders only; this project does not match orders or generate trades. Orders use integer-cent ticks internally and expose the requested double-price API. Buy prices must be in `[50.00, 99.99]`, sell prices in `[100.01, 150.00]`, quantities must be positive, and IDs must be unique. Invalid additions throw `std::invalid_argument`. Cancelling an unknown or already cancelled ID returns `false`.

## Build and run

```sh
g++ -std=c++17 -O2 -Wall -Wextra -pedantic order_book.cpp implementations.cpp bench.cpp -o bench
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
```

The fixed seed is 42. The same one-million-operation workload is used for verification and all implementations: approximately 70% adds, 20% cancels, 10% BBO reads, with depth-10 reads every 100 operations. Cancels choose a currently live order. Benchmark output reports the median of five runs and average nanoseconds per operation type. Timing includes the required per-operation clock measurement and periodic depth snapshots; use the same binary and host for comparisons.

## Implementations

| Impl | Orders and price levels | Best price | Cancellation lookup |
|---|---|---|---|
| A | `std::map<tick, bucket>`; each bucket has a FIFO `std::list` of resting orders and aggregate quantity | first/last map iterator | `unordered_map<ID, list iterator>` |
| B | Sorted contiguous vectors of aggregate price levels; orders are represented by ID records in an unordered map | vector front/back | `unordered_map<ID, {side,tick,qty}>`, then binary search |
| C | Fixed arrays indexed by tick, aggregate quantities, ID hash, and generation-checked priority queues | lazy cleanup of bid-max and ask-min heaps | `unordered_map<ID, {side,tick,qty}>` |

All complexities below use `L` for active price levels, `N` for live orders, `D` for requested depth, and assume expected O(1) unordered-map operations. For C, heap cleanup is amortized across BBO queries; its depth query scans the 10,001-slot tick range.

| Operation | A | B | C |
|---|---:|---:|---:|
| add_order | O(log L) | O(L) due to vector insertion | O(log L) on a new active level; otherwise expected O(1) |
| cancel_order | expected O(1) | O(L) due to vector erase | expected O(1) |
| get_bbo | O(1) | O(1) | amortized O(1), worst-case O(L log L) stale heap cleanup |
| get_bids / get_asks | O(D) | O(D) | O(10,001) scan |

## Correctness

`--verify` replays the identical deterministic workload against A, B, and C and checks BBO, top-ten bid/ask levels, prices, quantities, and cancellation outcomes after each operation. It also requests depth 10 every 100 operations. Unknown ID behavior is implemented in all three books.

## Benchmark and profiling

Run the commands above separately for A, B, and C. The benchmark runs five repetitions internally and prints the median. For Linux hardware counters, run on a Linux machine with `perf` access:

```sh
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl B
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl C
perf record -g ./bench --impl A && perf report
perf record -g ./bench --impl B && perf report
perf record -g ./bench --impl C && perf report
```

This run environment is macOS, where Linux `perf` is unavailable. No perf counters or perf call graphs are claimed here. Record actual Linux output in `REPORT.md` when available; do not treat machine-specific timing as portable.

## Submission / PR

Fork `KnightKnight27/scaler-HFT-C--2028` on GitHub, clone **your fork**, create a feature branch, place this project under `Capstone/`, commit and push the branch to your fork, then open a PR with the professor's repository as the base and your fork/branch as the head. Add your SST email and roll number above before opening the PR. Fill in `REPORT.md` with your own host's timing and Linux perf measurements before submission.
