# Capstone: Low-Latency Order Book

Student: Mohammed Fazil Momin  
Roll Number: `24BCS10278`  
SST Email: `mohammed.24bcs10278@sst.scaler.com`  


Three order books built on different data structures, one benchmark that runs
the same seeded workload through any of them, a `--verify` mode that checks they
agree, and the results in [REPORT.md](REPORT.md).

## Layout

```
include/order_book_common.hpp   Side, Level, BBO shared by all three
include/order_book_first.hpp    Implementation A: priority_queue + lazy deletion
include/order_book_second.hpp   Implementation B: multiset with price-time ordering
include/order_book_third.hpp    Implementation C: fixed arrays indexed by tick
src/order_book_first.cpp        unit test for A
src/order_book_second.cpp       unit test for B
src/order_book_third.cpp        unit test for C
src/bench.cpp                   benchmark + --verify
Makefile                        build targets
run_perf.sh                     runs tests, verify, bench, perf stat, perf record
results/                        captured outputs
REPORT.md                       results table and performance analysis
```

## Build

With make:

```
make            # bench, test_a, test_b, test_c
make test       # run the three unit tests
make verify     # ./bench --verify
```

Without make, one command per binary:

```
g++ -std=c++20 -O2 -Iinclude src/bench.cpp -o bench
g++ -std=c++20 -O2 -Iinclude src/order_book_first.cpp  -o test_a
g++ -std=c++20 -O2 -Iinclude src/order_book_second.cpp -o test_b
g++ -std=c++20 -O2 -Iinclude src/order_book_third.cpp  -o test_c
```

## Run

Correctness test (same 1,000,000-op workload through A, B and C, compared after
every operation):

```
./bench --verify
```

Benchmark one implementation (1,000,000 ops, seed 42, 5 runs, median reported):

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Optional flags: `--runs N`, `--ops N`, `--seed N`.

Full perf collection on Linux:

```
sh run_perf.sh
```

## Workload

Generated once with `std::mt19937 rng(42)` and replayed identically for every
implementation.

- 70% add, 20% cancel, 10% BBO query. A cancel is converted to an add when no
  order is live.
- Side 50/50. Quantity uniform in 1 to 100. Price a uniform valid tick inside the
  side's band.
- Cancels always pick a currently live order.
- Every 100 operations `get_bids(10)` and `get_asks(10)` are called.
- Each operation type is timed separately with `std::chrono::steady_clock`.
- The final BBO and a checksum folded over every BBO, cancel result and depth
  query are printed so the compiler cannot discard the work.

## Interface changes from the spec

The five required methods keep their signatures. Differences are internal or in
behaviour on invalid input.

- **Prices are stored as integer ticks** (`price * 100`). `add_order` still
  accepts a `double` and converts it. `Level` and `BBO` return doubles computed
  as `tick / 100.0`.
- **Invalid orders are silently rejected.** `add_order` returns `void`, so an
  order with a non-finite price, a price outside its side's band (bids 50.00 to
  99.99, asks 100.01 to 150.00), a price not on a 0.01 tick, or a quantity of
  zero or less does nothing.
- **Order ids are never reused.** Every id ever accepted is remembered. Adding
  an order with an id that was used before, even if cancelled, does nothing.
- **Time priority** is tracked by an internal arrival counter, not a caller
  supplied timestamp, because the required API has no timestamp parameter.
- `get_bbo`, `get_bids` and `get_asks` are `const`. In A the heaps are
  `mutable` because lazy deletion pops stale entries during a read.

## The three designs

### A: priority_queue with lazy deletion

- Orders: `unordered_map<id, Order>`.
- Price levels: `unordered_map<tick, total_qty>` per side.
- Best price: a `priority_queue` of orders per side. Cancelled orders are left
  in the heap and popped lazily when they reach the top.
- Cancel: hash lookup, subtract from level, erase from the order map.
- Depth: copy all live levels into a vector, sort, truncate.

| Operation | Complexity |
|---|---|
| add_order | O(log N) heap push, O(1) hash inserts |
| cancel_order | O(1) |
| get_bbo | amortised O(log N), stale pops |
| get_bids / get_asks | O(L log L), L = live levels |

### B: multiset ordered by price then arrival

- Orders: one `std::multiset<Order>` per side, ordered by price, then arrival
  sequence, then id. This is the only design that keeps individual orders in
  price-time order.
- Price levels: not stored. A level is the run of consecutive orders with the
  same tick.
- Best price: `begin()` of the set.
- Cancel: `unordered_map<id, multiset::iterator>` gives the exact node, erased
  directly.
- Depth: walk the set from the front summing runs of equal ticks.

| Operation | Complexity |
|---|---|
| add_order | O(log N) |
| cancel_order | O(log N) |
| get_bbo | O(k), k = orders at the best level |
| get_bids / get_asks | O(orders in the top `depth` levels) |

### C: fixed arrays indexed by tick

- Price levels: two `std::vector<PriceLevel>` of exactly 5,000 slots, one per
  side. The tick is the index, so no search and no allocation to find a level.
- Orders: each `PriceLevel` holds `total_qty` and a `std::list<Order>` in
  arrival order.
- Best price: two integers, `best_bid_` and `best_ask_`, updated on add. On a
  cancel that empties the best level, the array is walked toward the mid price
  until a non-empty level is found.
- Cancel: `unordered_map<id, list::iterator>` gives the node, erased in O(1).
- Depth: walk the array from the best price collecting non-empty levels.

| Operation | Complexity |
|---|---|
| add_order | O(1) |
| cancel_order | O(1), plus O(gap) when the best level empties |
| get_bbo | O(1) |
| get_bids / get_asks | O(ticks scanned to find `depth` levels) |

## Benchmark harness note

The three books are called through a small abstract `Book` interface so that
one loop drives all of them. Every implementation pays the same virtual call
and the same two `steady_clock::now()` calls per operation, so the overhead is
constant across A, B and C and does not change the ranking.

## Correctness output

```
Running correctness test over 1000000 operations...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

See `results/` for captured benchmark and perf outputs.
