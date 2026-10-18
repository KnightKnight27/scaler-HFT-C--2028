# Capstone: Low-Latency Order Book

**Team**

| Name | Roll No | SST Email |
|---|---|---|
| Harshit Tiwari | 24BCS10277 | harshit.24bcs10277@sst.scaler.com |
| Tejas Varshney | 24BCS10313 | tejas.24bcs10313@sst.scaler.com |

A resting order book for one instrument (no matching), built three times with
different data structures, checked against each other, benchmarked on the same
1,000,000-operation workload and profiled with Linux `perf`.

The results table and the performance analysis are in [REPORT.md](REPORT.md).

## Layout

```
include/common.hpp       Side, Level, BBO, tick helpers and price bands
include/book_map.hpp     Implementation A  (MapBook)
include/book_vector.hpp  Implementation B  (VectorBook)
include/book_array.hpp   Implementation C  (ArrayBook, our own design)
src/workload.hpp         seeded workload generator (shared by bench and verify)
src/verify.hpp           correctness test (--verify)
src/bench.cpp            the single benchmark program
scripts/run_all.sh       build, verify, bench, perf stat, perf record, extra experiments
results/                 all outputs used in the report
```

## Build

Needs g++ with C++17. Only the standard library is used.

```bash
make            # builds ./bench and ./bench_prof
```

or with a single command:

```bash
g++ -std=c++17 -O2 -Iinclude -Isrc src/bench.cpp -o bench
```

`bench_prof` is the same code built with `-fno-omit-frame-pointer`, only used for
`perf record -g` so call graphs are readable. Both builds use `-O2`.

## Run

```bash
./bench --verify             # correctness test, all three implementations
./bench --impl A             # benchmark one implementation (5 runs, median)
./bench --impl B
./bench --impl C
./bench --impl C --runs 1    # single run, used under perf stat -r 5

bash scripts/run_all.sh      # everything, results go to results/
```

If `perf` on your machine cannot resolve symbols (the WSL kernel `perf` is built
without libelf), point the script at another binary: `PERF=/path/to/perf bash scripts/run_all.sh`.

## Interface changes

The API is the one from the assignment, with these changes:

1. `add_order` returns `bool` instead of `void`. It returns `false` and leaves the
   book unchanged if the price is not a multiple of 0.01, is outside the band for
   that side (bids 50.00 to 99.99, asks 100.01 to 150.00), the quantity is not
   positive, or the id is already live.
2. The three classes are called `MapBook`, `VectorBook` and `ArrayBook`. They have
   the same methods, and the benchmark is a template over the book type, so there
   is no virtual call in the timed code.

Prices are still passed and returned as `double`. Inside every implementation a
price is converted once to an integer tick (`100.01 -> 10001`), and all
comparisons and lookups use ticks.

## The three designs

| | A: `MapBook` | B: `VectorBook` | C: `ArrayBook` |
|---|---|---|---|
| Price levels | `std::map<tick, qty>` per side (red-black tree) | sorted `std::vector<{tick, qty}>` per side | one flat `std::vector<long>` indexed by tick (10,001 entries) |
| Orders | `std::unordered_map<id, Order>` (chained hash, one node per order) | `std::map<id, Order>` (tree, one node per order) | own open-addressing hash table, linear probing, slots stored inline in one array |
| Best bid/ask | `begin()` of each map | `back()` of each vector (bids ascending, asks descending) | cached `best_bid_` / `best_ask_` ticks |
| Find order on cancel | hash lookup | tree search by id | hash to a slot, probe forward |

### A: std::map + std::unordered_map

- Orders: `id -> {side, tick, qty}` in an `unordered_map`.
- Levels: `bids_` is a `std::map` ordered with `std::greater` and `asks_` with
  `std::less`, so the best price of each side is always `begin()`. A level is
  erased when its quantity reaches zero.
- Cancel: find the order in the hash map, erase it, then find its tick in the
  side's map and subtract the quantity.

### B: sorted vector + std::map

- Orders: `id -> {side, tick, qty}` in a `std::map`, so lookups are a tree search
  instead of hashing.
- Levels: each side is a contiguous sorted vector of `{tick, qty}`. Bids are kept
  ascending and asks descending, so in both the best price is the last element.
  `std::lower_bound` finds a tick. A new level is inserted in place, shifting
  the worse levels; since the shift is towards the back, a new best price is the
  cheapest insert.
- Cancel: tree search by id, binary search for the tick, erase the level if it
  becomes empty.

### C: flat tick array + open-addressing table (own design)

- Levels: total quantity for every tick from 50.00 to 150.00 lives in one array,
  so finding a level is just `level_qty_[tick - 5000]`. No allocation ever
  happens for levels.
- Best price: `best_bid_` and `best_ask_` are cached. Adding at a better price
  updates them. When a cancel empties the best level, the code walks the array
  away from the mid until it finds a non-empty level.
- Orders: our own hash table. Each slot holds `{id, qty, tick}` inline (24 bytes),
  the index is `(id * 0x9E3779B97F4A7C15) >> shift` (Fibonacci hashing), and
  collisions go to the next slot. A cancelled slot becomes a tombstone. When
  live + tombstones exceeds half the table, it is rebuilt (doubled if more than
  a quarter is live, otherwise the same size to clear tombstones).

### Complexity

`L` = non-empty price levels on one side (at most 5,000), `N` = live orders,
`d` = requested depth, `P` = ticks on one side (5,000).

| Operation | A | B | C |
|---|---|---|---|
| add_order | O(log L) + O(1) avg | O(log N) + O(log L), plus O(L) shift for a new level | O(1) avg (amortised over rebuilds) |
| cancel_order | O(1) avg + O(log L) | O(log N) + O(log L), plus O(L) shift if the level empties | O(1) avg, plus a scan of up to O(P) if the best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids / get_asks | O(d) | O(d) | O(d + empty ticks skipped), worst O(P) |

In this workload the book is dense (almost all 5,000 ticks per side have
orders), so in C the scan after emptying the best level is normally one step.

## Correctness

`./bench --verify` runs three workloads through all three books and through a
small deliberately naive reference book (no cached best, every query scans the
price range):

- the benchmark workload (1,000,000 ops, seed 42),
- a churn workload (300,000 ops, 20 ticks wide, 45% cancels) where levels empty
  and the best price moves all the time,
- a tiny book (100,000 ops, 3 ticks wide) that is often empty on one or both sides.

After every operation it compares the add/cancel return value, the BBO, the top
10 bids and the top 10 asks, and checks that cancelling the same id again returns
`false`. Full depth is compared every 10,000 operations. There are also fixed
tests for the empty book, unknown and repeated cancels, depth shorter than
requested, quantities summed at one price, the best price moving after a cancel,
and rejected prices.

```
Running correctness test...
Workloads: 1,000,000 ops (seed 42), 300,000 ops churn (seed 7), 100,000 ops tiny book (seed 99)
Checked after every op: add/cancel result, BBO, top-10 bids and asks, repeated cancel.
Full depth checked every 10,000 ops. Edge cases: empty book, unknown ids, short depth, bad prices.

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

The benchmark also prints the final BBO and a checksum of every result. All
three print the same values (`Bid 99.99 x 2525`, `Ask 100.01 x 2573`,
checksum `12647226763645828079`).

## Benchmark

- 1,000,000 operations from `std::mt19937(42)`: about 70% add, 20% cancel, 10%
  `get_bbo`, and `get_bids(10)` + `get_asks(10)` after every 100 operations.
- Side 50/50, quantity 1 to 100, price uniform over the side's band. A cancel
  always picks a random live order.
- The workload is generated before timing, so all three books see the same
  sequence.
- Each run has two passes on a fresh book. One times the whole loop with a single
  clock (total time). The other wraps each call in `steady_clock::now()` (per-op
  average, p50, p99). Each `--impl` invocation does 5 runs and prints the median.

Results are in [REPORT.md](REPORT.md) and the raw output is in `results/`.
