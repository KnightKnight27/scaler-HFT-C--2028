# Project Brief: Low-Latency Order Book

This project implements and compares three single-threaded C++17 order books for one financial instrument. All three expose the same logical operations, consume the same deterministic workload, and are checked against one another before they are benchmarked.

The project is primarily a data-structure and systems-performance exercise. It demonstrates integer price representation, hash-based identity lookup, ordered containers, priority queues, lazy deletion, fixed-size arrays, cached state, deterministic testing, timing methodology, and the limits of hardware-level performance claims.

## 1. Assignment scope and domain model

The book is resting-only:

- There is one instrument and no symbol routing.
- Orders are added and remain in the book until cancelled.
- There is no matching engine, trade generation, partial-fill logic, or threading.
- Multiple orders may share a price. A price level stores their summed quantity.
- Order IDs are unique and never reused, even after cancellation.
- The implementation uses only the C++ standard library for the order-book logic.

The price domain is fixed by the assignment:

| Domain | Tick range | Decimal range |
|---|---:|---:|
| Buy/bid | 5000..9999 | 50.00..99.99 |
| Mid price | 10000 | 100.00, not allowed |
| Sell/ask | 10001..15000 | 100.01..150.00 |

The minimum price increment is 0.01. A tick is the integer price multiplied by 100, so 100.01 is stored as `10001`.

## 2. Shared interface and representation

The public model is defined in `include/order_book.hpp`:

```cpp
enum class Side { Buy, Sell };
using Tick = int;
using OrderId = std::uint64_t;

struct Level {
    Tick price;
    long quantity;
};

struct BBO {
    bool has_bid = false;
    Tick bid_price = 0;
    long bid_quantity = 0;
    bool has_ask = false;
    Tick ask_price = 0;
    long ask_quantity = 0;
};

class OrderBook {
public:
    virtual ~OrderBook() = default;
    virtual void add_order(OrderId, Side, Tick, long) = 0;
    virtual bool cancel_order(OrderId) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};
```

The assignment suggested `double` prices, but this project intentionally changes the interface to integer `Tick` prices. This avoids floating-point equality and ordering problems and makes the fixed price universe directly indexable. Human-readable decimal formatting is a presentation concern, handled by `include/price.hpp`.

`StoredOrder` is the cancellation index:

```cpp
struct StoredOrder {
    Side side;
    Tick price;
    long quantity;
};
```

Every implementation stores live orders by `OrderId`. A cancellation can therefore find the exact side, price, and quantity through expected O(1) hash lookup instead of scanning every price level.

## 3. Invariants and validation

The implementations must preserve these invariants:

1. Every live order has a unique ID.
2. A used ID cannot be added again, including after cancellation.
3. Every quantity is positive.
4. Buy prices stay in `5000..9999`; sell prices stay in `10001..15000`.
5. A level quantity equals the sum of its live orders.
6. Empty levels are removed from the authoritative representation, or represented by zero in the array design.
7. The best bid is the highest live bid tick.
8. The best ask is the lowest live ask tick.
9. Cancelling an unknown or already-cancelled order returns `false` and does not change the book.
10. An empty book reports `has_bid == false` and `has_ask == false`.

`validate_order` enforces quantity and side-specific price ranges. `validate_new_id` uses a permanent `std::unordered_set<OrderId>` so that deleting a live-order record does not accidentally permit prohibited ID reuse.

## 4. Implementation A: ordered maps

Source: `src/map_order_book.hpp`.

Data structures:

- `std::map<Tick, long> bids_` for bid price levels.
- `std::map<Tick, long> asks_` for ask price levels.
- `std::unordered_map<OrderId, StoredOrder> orders_` for live cancellation lookup.
- `std::unordered_set<OrderId> used_ids_` for the never-reuse rule.

The maps are authoritative. Adding an order increments the selected map level. Cancelling an order looks up its metadata, subtracts its quantity, erases the level if it becomes empty, and erases the live-order record.

Best-price lookup uses the map endpoints: `bids_.rbegin()` is the highest bid and `asks_.begin()` is the lowest ask. Depth walks the bid map in reverse order or the ask map in forward order and stops after the requested number of levels.

This implementation is the simplest ordered baseline. It provides sorted levels naturally, but map nodes introduce pointer indirection and dynamic allocation.

## 5. Implementation B: maps plus lazy heaps

Source: `src/heap_order_book.hpp`.

Data structures:

- Authoritative `std::map<Tick, long>` containers for current quantities.
- A max `std::priority_queue` for bid prices.
- A min `std::priority_queue` for ask prices.
- Generation maps that distinguish current heap entries from stale entries.
- The same live-order hash map and permanent used-ID set as implementation A.

When a previously empty price level receives an order, its tick and current generation are pushed into the relevant heap. Cancelling an order updates the authoritative map immediately, but does not remove an arbitrary heap entry. That is the lazy-deletion trade-off.

When a BBO or depth query reads a heap, it copies the heap and repeatedly removes entries that are either no longer present in the authoritative level map or carry an older generation than the current level. If a level is emptied and later re-added, its generation changes, so the old heap entry cannot become visible again.

This avoids an expensive arbitrary heap deletion operation, at the cost of stale entries, generation bookkeeping, heap copying during `const` queries, and cleanup work on reads.

## 6. Implementation C: fixed price-indexed arrays

Source: `src/array_order_book.hpp`.

Data structures:

- `std::array<long, 5000> bids_` indexed by `tick - 5000`.
- `std::array<long, 5000> asks_` indexed by `tick - 10001`.
- `best_bid_` and `best_ask_` cached as scalar ticks, using `-1` internally for an empty side.
- The same live-order hash map and permanent used-ID set as A and B.

The two arrays cover the assignment's 10,000 legal price levels without storing the forbidden mid tick. Price-level access is direct arithmetic followed by an array access; it does not need tree traversal or a price-level hash lookup.

On add, the relevant quantity is incremented and the cached best tick is updated. On cancel, the quantity is decremented. A best-price recovery scan runs only when the emptied level was the current best. Bids scan downward and asks scan upward until another non-zero level is found or the valid range ends.

Depth queries walk from the cached best tick and skip zero-quantity slots. The empty-side check before scanning is important: it prevents an empty sentinel from being used as an array index.

The array design has predictable fixed price storage and good locality, but it uses space for the complete fixed domain and can scan as many as 5,000 ticks while recovering a best price or producing sparse depth.

## 7. Complexity comparison

Let `P` be the number of populated price levels, `H` the number of heap entries, and `D` the requested depth. Hash-map operations are expected O(1), not worst-case guaranteed O(1).

| Operation | A: maps | B: lazy heaps | C: arrays |
|---|---:|---:|---:|
| Add | O(log P) | O(log P) plus possible heap push | O(1) expected, including hash lookup |
| Cancel | O(log P) plus expected O(1) ID lookup | O(log P) plus expected O(1) ID lookup | O(1) expected, plus best recovery when needed |
| BBO | O(1) | O(H) in this implementation because the heap is copied and stale entries may be removed | O(1) |
| Bid/ask depth | O(D) after ordered traversal starts | O(D log H) approximately, including copied-heap cleanup | O(D + scanned tick range), bounded by 5,000 per side |

The table describes the implemented code, not an idealized alternative. In particular, implementation B's BBO is not a pure O(1) operation because `live_top` takes the heap by value to preserve `const` query semantics.

## 8. Deterministic workload

The workload generator is in `bench/workload.hpp`. It creates 1,000,000 operations using `std::mt19937` with seed `42`:

- approximately 70% adds;
- approximately 20% cancellations;
- approximately 10% BBO queries;
- quantity uniformly distributed from 1 to 100;
- equal buy/sell selection;
- prices uniformly selected from the valid side range;
- cancellations selected only from currently live IDs.

The live-ID vector uses swap-and-pop when removing a cancelled ID. Since the order of that pool is irrelevant, this avoids shifting the remaining vector elements and gives expected O(1) removal. The generated operation vector reserves space for the full workload to avoid repeated capacity growth.

Every 100 operations, the benchmark and cross-implementation test also request bid depth 10 and ask depth 10. The exact operation stream is reused for all implementations, so the comparison changes the data structure rather than the input.

## 9. Correctness strategy

There are two layers of automated checks.

`tests/order_book_contract.cpp` runs the same behavioral contract against A, B, and C. It checks empty BBO behavior, aggregation, best-price selection, depth ordering, successful cancellation, unknown and repeated cancellation, reused-ID rejection, invalid prices, non-positive quantity, and heap cancellation/re-add behavior.

`tests/cross_implementation.cpp` applies the complete 1,000,000-operation stream to all three books. After every operation it compares BBO, bid levels, ask levels, prices, and quantities. Depth is also checked every 100 operations.

The benchmark has an equivalent `--verify` mode and reports:

```text
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

## 10. Benchmark methodology

The benchmark is implemented in `bench/benchmark.hpp` and selected through one executable:

```bash
./build/bench --verify
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
```

Timing uses `std::chrono::steady_clock`. The benchmark records total execution time and average nanoseconds per add, cancel, and BBO query. Each implementation runs five times and reports the median. A checksum consumes query results and cancellation outcomes, and the final BBO is printed so the work remains observable and final state can be compared.

The timed operation measurements include a clock read around each individual operation. That is useful for a simple comparative assignment, but it adds overhead, especially to very small BBO calls. The numbers are local comparative measurements, not universal hardware-independent latency guarantees.

## 11. Current measured results

The current captured results are in `reports/benchmark-results.md`. On the local C++17 `g++ -O2` run:

| Metric | A: map | B: lazy heap | C: arrays |
|---|---:|---:|---:|
| Total time, median ms | 493.049 | 2710.26 | 247.157 |
| ns / add, median | 454.580 | 1122.790 | 183.400 |
| ns / cancel, median | 734.435 | 1297.540 | 469.444 |
| ns / BBO, median | 42.3489 | 14019.5 | 33.6499 |

All runs produced checksum `4496618155` and final bid tick `9999` / ask tick `10001`.

For this workload, C is fastest overall and for each measured operation. A is the middle baseline. B is slowest, particularly for BBO, because its query path copies the heap and cleans stale entries. These results explain this workload and machine; they do not prove that C is best for every price domain, workload, or CPU.

## 12. Performance reasoning and profiling limits

Expected structural effects are:

- map nodes involve pointer chasing, comparisons, and allocation-backed storage;
- heaps add comparator work, indirect storage, stale-entry checks, and generation bookkeeping;
- arrays provide contiguous fixed price-level storage and direct indexing;
- cached best ticks make the normal array BBO path a few scalar reads;
- array best-price recovery and sparse depth still have bounded scans;
- hash-table behavior remains relevant for order-ID lookup in all three designs.

These are design-based explanations, not substitutes for hardware counters. The assignment's profiling commands are documented in `README.md`:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./build/bench --impl A
perf record -g ./build/bench --impl A
perf report
```

The same commands should be repeated for B and C. In the current environment `perf` was not installed, so IPC, branch misses, cache misses, L1 data-cache misses, dTLB misses, and page faults are unavailable. They must remain reported as unavailable rather than being inferred from source code or replaced with invented values.

## 13. Build and project layout

Build and test:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Important files:

```text
include/order_book.hpp       shared types, interface, validation
include/price.hpp            decimal display helper
src/map_order_book.hpp       implementation A
src/heap_order_book.hpp      implementation B
src/array_order_book.hpp     implementation C
bench/workload.hpp           deterministic operations
bench/benchmark.hpp          timing, checksum, median reporting
bench/main.cpp               benchmark and verification CLI
tests/order_book_contract.cpp common behavioral checks
tests/cross_implementation.cpp long cross-implementation check
reports/                     captured results and analysis
```

## 14. Concepts demonstrated

This project is a compact revision exercise for:

- domain modeling: orders, sides, ticks, levels, BBO, and depth;
- interface-based polymorphism through the shared `OrderBook` abstraction;
- RAII and `std::unique_ptr` ownership in the test and benchmark factories;
- integer/fixed-point representation for exact price comparisons;
- `std::map` ordering and endpoint access;
- hash maps for expected O(1) identity lookup;
- hash sets for permanent uniqueness constraints;
- `std::priority_queue` max/min ordering;
- lazy deletion and generation/version validation;
- fixed-size `std::array` storage and offset indexing;
- cached derived state and conditional best-price recovery;
- bounded depth traversal;
- deterministic pseudo-random testing;
- invariant and contract testing with assertions;
- cross-implementation differential testing;
- `std::chrono::steady_clock` timing;
- median aggregation across repeated runs;
- checksum-based benchmark observability;
- CMake and CTest build/test automation;
- Linux `perf` methodology and honest unavailable-metric reporting;
- separating measured facts from performance hypotheses.

## 15. Reasonable next experiments

These are deliberately outside the current implementation but follow naturally from the assignment:

- collect `perf` counters on a Linux machine where the tool is available;
- measure p50, p95, p99, and maximum latency instead of only medians;
- batch timing around groups of operations to reduce per-operation clock overhead;
- reserve order-ID hash-table capacity and measure allocation effects;
- compare the array scan with an occupancy bitmap or next-occupied-index structure;
- vary price sparsity, cancellation rate, and depth request size;
- test a dynamic price domain where fixed arrays may waste memory.

The current scope intentionally does not add matching, multi-threading, custom allocators, object pools, external libraries, or speculative optimizations without measurements.
