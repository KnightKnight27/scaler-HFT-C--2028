# Low-Latency Order Book Design

## Goal

Build and compare three correct, single-threaded C++17 order-book implementations for one instrument, using the same repeatable workload and profiling procedure.

## Scope

The system supports adding orders, cancelling live orders, reading the BBO, and reading bounded bid/ask depth. It does not match orders, generate trades, support multiple instruments, or use external order-book libraries.

The valid price domain is bid ticks `5000..9999`, ask ticks `10001..15000`, with mid-price tick `10000`. Quantities are positive `long` values. Order IDs are unique and never reused.

## Public model

The shared logical interface is:

```cpp
enum class Side { Buy, Sell };
using Tick = int;
using OrderId = std::uint64_t;

struct Level { Tick price; long quantity; };

struct BBO {
    bool has_bid;
    Tick bid_price;
    long bid_quantity;
    bool has_ask;
    Tick ask_price;
    long ask_quantity;
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

The assignment's `double price` fields are intentionally replaced by `Tick`. README output will convert ticks to decimal prices for humans.

## Implementations

### A: ordered map

- Store price-level quantities in separate ascending `std::map<Tick, long>` containers for bids and asks.
- Store live order metadata in `std::unordered_map<OrderId, Order>`.
- Find the best bid with `rbegin()` and the best ask with `begin()`.
- Cancel by looking up the order ID, subtracting its quantity from the side map, erasing empty levels, then erasing the ID.

### B: heap with lazy deletion

- Store level quantities in side maps for authoritative totals.
- Store price entries in a max-heap for bids and min-heap for asks.
- Push a new heap entry when a previously empty price level receives an order.
- On BBO/depth access, discard heap entries whose current side-map quantity is zero or whose tick no longer matches the authoritative level.
- Store live order metadata in the same ID lookup table used by A.

Cancellation is immediate in the authoritative structures; stale heap entries are cleaned only when read. This is the deliberate trade-off that makes B structurally different without adding a heap-indexing subsystem.

### C: fixed price-indexed arrays

- Use one 5,000-entry bid array indexed by `tick - 5000` and one 5,000-entry ask array indexed by `tick - 10001`; together they represent the fixed 10,000 valid price levels.
- Store live order metadata in an `std::unordered_map<OrderId, Order>`.
- Maintain best bid and best ask ticks as scalar fields, updating them on add and scanning toward the next occupied tick only when the best level becomes empty.
- Return depth by walking the bounded tick range from the current best.

This design uses the assignment's fixed price universe directly, giving predictable storage and contiguous price-level access. A best-price recovery scan is bounded by 5,000 entries on the affected side.

## Complexity targets

Let `P` be populated price levels and `D` the requested depth.

| Operation | A map | B heap | C arrays |
|---|---:|---:|---:|
| add | O(log P) | O(log P) | O(1) typical |
| cancel | O(log P) | O(log P) | O(1) typical |
| BBO | O(1) | O(1) amortized | O(1) typical |
| bids/asks | O(D log P) | O(D log P) amortized | O(D + bounded scan) |

The order-ID hash lookup is expected O(1) in all designs. Array best-price recovery has a bounded worst case over the fixed domain; the report must distinguish that from typical cost.

## Program layout

```text
include/order_book.hpp       Shared types and interface
include/price.hpp            Tick validation and display conversion
src/map_order_book.hpp       Implementation A
src/heap_order_book.hpp      Implementation B
src/array_order_book.hpp     Implementation C
bench/workload.hpp           Fixed-seed workload generator
bench/benchmark.hpp          Timing, checksum, and median reporting
bench/main.cpp               --verify and --impl A/B/C entry points
tests/correctness.hpp        Shared behavioral and cross-implementation checks
reports/                     Captured results and analysis
README.md                    Build and usage instructions
```

## Workload and verification

The benchmark runs 1,000,000 operations from `std::mt19937(42)`, approximately 70% adds, 20% cancellations of currently live orders, and 10% BBO queries. Every 100 operations it calls both depth queries. The exact generated operation stream is reused for all implementations.

`--verify` runs the stream against all three implementations and compares BBO state, depth levels, prices, and quantities after every observable operation. Unknown cancellation must return `false`; empty BBOs must report both sides absent; depth requests return only available levels.

## Measurement

Use `std::chrono::steady_clock`, separate counters for add/cancel/BBO, five runs per implementation, and report medians. Print a final BBO and checksum. Run `perf stat -r 5` with the assignment's event list where available and document unavailable counters. Run `perf record -g` and use `perf report` to identify hot paths.

## Deliverables

- Three implementations and one benchmark executable.
- Automatic correctness output showing A/B/C agreement.
- README with build, run commands, interface changes, and data-structure choices.
- Captured benchmark and `perf` results.
- One-to-two-page performance analysis covering latency, branch misses, cache behavior, page faults, production choice, and next experiments.

## Non-goals

No threading, matching engine, trade generation, symbols, external dependencies, object pool, custom allocator, or speculative optimization before measurements.
