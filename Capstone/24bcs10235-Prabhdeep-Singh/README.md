# Low-Latency Order Book

|                |                                          |
|----------------|------------------------------------------|
| **Name**       | Prabhdeep Singh                          |
| **Roll no.**   | 24BCS10235                               |
| **SST email**  | prabhdeep.24bcs10235@sst.scaler.com      |

A single-instrument resting order book (no matching) built three ways, checked against each other,
benchmarked on one shared workload and profiled. The results table and the performance analysis
(questions 11.1-11.6) are in [`REPORT.md`](REPORT.md).

## Files

| File | Purpose |
|---|---|
| `orderbook.h` | `Side`, `Level`, `BBO`, price band constants, `price_to_tick` / `tick_to_price` |
| `impl_a.h` | **A**: `std::map` price levels |
| `impl_b.h` | **B**: `unordered_map` price levels + tracked best price |
| `impl_c.h` | **C** (own design): price-indexed array + flat open-addressing order table |
| `bench.cpp` | one program: `--verify` and `--impl A/B/C` |
| `Makefile` | `g++ -std=c++17 -O2 -Wall -Wextra` |
| `results/` | raw output of one run: `verify.txt`, `bench_{A,B,C}.txt` (with the Mac's CPU counters), `perf_stat_*.txt`, `perf_report_*.txt` |

## Build

```bash
make                                          # builds ./bench
# or, without make:
g++ -std=c++17 -O2 -Wall -Wextra -o bench bench.cpp
```

Header-only, C++ standard library only, single-threaded. No warnings with `-Wall -Wextra`.

## Run

```bash
./bench --verify         # correctness: all three implementations on the same workload
./bench --impl A         # benchmark A: 5 runs, median reported
./bench --impl B
./bench --impl C
```

Workload (identical for all three): 1,000,000 operations from `std::mt19937 rng(42)`: 70% add,
20% cancel (always a live order), 10% BBO query, plus `get_bids(10)` and `get_asks(10)` every 100
operations. Side is 50/50, quantity 1-100, price a random valid tick in the side's band.

Profiling (Linux), as in the assignment:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A   # then B, C
perf record -g ./bench --impl A && perf report                                                                                                        # then B, C
```

## Interface

The required five operations are kept, with these differences from the assignment's sketch:

- **Prices are converted to integer ticks on entry** (`tick = round(price * 100)`, so 99.99 becomes 9999).
  Everything inside the books uses `int` ticks. `Level` and `BBO` still return `double` prices.
- **Price bands are a precondition of `add_order`:** bids 50.00-99.99, asks 100.01-150.00 (tick 10000 = the
  100.00 mid is never used). Each implementation `assert`s this. Out-of-band prices are not handled.
- **No common base class.** `OrderBookA/B/C` are separate header-only classes with the same five
  functions; `bench.cpp` uses templates, so there is no virtual-call cost in the measurement.
- `get_bids` / `get_asks` return levels best first, at most `depth` of them (fewer if the book is smaller).
- Order IDs must be unique and are never reused (as specified). Cancelling an unknown or already
  cancelled ID returns `false`.

## Data structures

All three keep one total quantity per price level (the sum of the resting orders at that price) and a
lookup from order ID to `{side, price, qty}` so that cancel is fast. They differ in how they store the
levels, how they find the best price, and (for C) how they locate an order.

`L` = number of non-empty price levels on one side (at most 5,000). `D` = requested depth.

### A: `std::map` price levels

| Question | Answer |
|---|---|
| Data structures | `std::map<int,long>` per side (red-black tree), `std::unordered_map<id, {side,tick,qty}>` |
| Orders stored | in the hash map, one node per live order |
| Price levels stored | tick -> total qty, one tree node per level, sorted at all times |
| Best bid / ask | `rbegin()` / `begin()` of the tree |
| Cancel lookup | hash lookup by ID, then `find` on the tree by tick; the level is erased when it reaches 0 |

| Operation | Complexity |
|---|---|
| `add_order` | O(log L) (+ O(1) average hash insert) |
| `cancel_order` | O(log L) (+ O(1) average hash lookup) |
| `get_bbo` | O(1); with libc++ `rbegin()` walks a short right spine, O(log L) |
| `get_bids` / `get_asks` | O(D) |

### B: `unordered_map` price levels + tracked best

| Question | Answer |
|---|---|
| Data structures | `std::unordered_map<int,long>` per side, `std::unordered_map<id, {side,tick,qty}>`, two cached ints `best_bid_` / `best_ask_` |
| Orders stored | same hash map as A |
| Price levels stored | tick -> total qty in a hash table: **no ordering** |
| Best bid / ask | cached integers: updated on add by comparison; when the best level is emptied by a cancel, the whole level table is scanned for the new best |
| Cancel lookup | hash lookup by ID, then hash lookup of the level |

| Operation | Complexity |
|---|---|
| `add_order` | O(1) average |
| `cancel_order` | O(1) average; O(L) when the best level empties |
| `get_bbo` | O(1) average (cached tick + one hash lookup for the quantity) |
| `get_bids` / `get_asks` | O(L log L): copy every level into a vector and sort it, because a hash table has no order |

### C: price-indexed array + flat order table (own design)

| Question | Answer |
|---|---|
| Data structures | `std::array<long, 10001>` indexed by `tick - 5000`; a flat open-addressing hash table of 24-byte `{id, qty, idx, side}` slots; two integer cursors |
| Orders stored | in the flat table: one contiguous `std::vector<Slot>`, linear probing, Fibonacci hashing, load factor kept <= 0.5 (doubles when needed), backward-shift deletion (no tombstones) |
| Price levels stored | the array itself: index = tick - 5000, value = total qty. 80 KB, always allocated, no per-level allocation |
| Best bid / ask | `best_bid_` / `best_ask_` cursors. When the best level empties, the cursor walks over neighbouring array slots to the next non-empty level |
| Cancel lookup | multiplicative hash of the ID -> probe the flat table -> the slot already holds the array index and quantity, so the level update is one array write |

| Operation | Complexity |
|---|---|
| `add_order` | O(1) expected, amortised (the table doubles 4 times during the benchmark) |
| `cancel_order` | O(1) expected + cursor re-scan only when the best level empties (cost = number of empty ticks skipped) |
| `get_bbo` | O(1) |
| `get_bids` / `get_asks` | O(D + empty ticks skipped) |

**How C differs from A and B.** A and B both use `std::unordered_map` (separate chaining, one heap node
per order) to find orders. C replaces it with its own contiguous open-addressing table, replaces the
tree/hash of levels with a direct-indexed array, and finds the best price with a cursor over that array.

## Correctness results

`./bench --verify` (full output in [`results/verify.txt`](results/verify.txt)):

```
Running correctness test...

320003 comparisons on 1000000 random operations, 0 mismatch(es)

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

The same random workload runs through A, B and C and every result is compared: each `cancel_order` return
value, each BBO (prices and quantities), and `get_bids(10)` / `get_asks(10)` every 100 operations. If one
implementation disagrees with the other two, it is the one reported as FAIL. Before that, each
implementation is checked against explicit expected values for the behaviours the assignment calls out:
an empty book (`has_bid` / `has_ask` false), cancelling an unknown or already-cancelled ID (returns
`false`), fewer levels than the requested depth, and several orders at one price adding up. All three also
finish the benchmark with the same checksum.

## Results

Apple M4, macOS, `g++` = Apple clang 21 (libc++), `-O2`, one thread, median of 5 runs. The full results
table, `perf` data and analysis are in [`REPORT.md`](REPORT.md).

| Metric | A: map | B: hash | C: array |
|---|---|---|---|
| Total time, whole workload (ms) | 112.1 | 1694.8 | 41.5 |
| ns / add | 81.6 | 50.1 | 28.7 |
| ns / cancel | 184.9 | 194.4 | 20.8 |
| ns / BBO | 13.8 | 13.9 | 12.7 |

Total time is wall-clock for the whole workload, including the `get_bids` / `get_asks` calls. `steady_clock`
on this Mac only advances in steps of about 42 ns and each per-operation number includes one pair of `now()`
calls, so single measurements are coarse. Averages over 100k+ calls are still meaningful, but the three BBO
numbers are mostly timer and cannot be told apart.
