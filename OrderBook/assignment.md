# Capstone: Build a Low-Latency Order Book

(original brief, kept here for reference)

Level: Intermediate C++ · Duration: 2 weeks · Language: C++17 or later

## 1. What is this about

An order book holds buy orders (bids) and sell orders (asks).
Best bid = highest buy price. Best ask = lowest sell price. BBO = both.

Build an order book for one instrument, three different ways, benchmark all
three, profile with Linux `perf`, and explain why one beats another.

## 2. Rules

- One instrument, no symbols.
- Tick size 0.01, prices are multiples of 0.01.
- Bid range [50.00, 99.99], ask range [100.01, 150.00], mid 100.00.
- 10000 possible price levels.
- No matching, no trades. Orders rest until cancelled.
- Many orders can sit at the same price; the level qty is the sum.
- Order ids are unique and never reused.
- Single threaded, standard library only, compile with `-O2`.

## 3. Required API

```cpp
enum class Side { Buy, Sell };

struct Level { double price; long qty; };

struct BBO {
    bool has_bid; double bid_price; long bid_qty;
    bool has_ask; double ask_price; long ask_qty;
};

class OrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};
```

May be modified (e.g. integer ticks), but document any change.

Required behaviour: cancelling an unknown/already-cancelled order returns
`false` and must not crash; empty book gives `has_bid == has_ask == false`;
asking for more depth than exists returns what's there.

## 4. Three implementations

Three genuinely different data structures (how they store orders, store price
levels, find the best price, and find an order on cancel). Implementation C
must be your own design. Document, per implementation, the data structures and
the Big-O of add / cancel / get_bbo / get_bids / get_asks.

## 5. Correctness check

Run the same random workload through all three and verify BBO, bid levels, ask
levels, quantities and prices match. `./bench --verify` or a separate test.

## 6. Benchmark

One benchmark program with the implementation selected from the CLI
(`./bench --impl A|B|C`). 1,000,000 operations, fixed seed, ~70% add / 20%
cancel / 10% BBO, and `get_bids(10)` + `get_asks(10)` every 100 ops. Cancels
must pick a live order. Side 50/50, qty 1..100, price a valid random tick.

## 7. Measurements

`steady_clock`; total time, avg ns per add, per cancel, per get_bbo; 5 runs;
report the median; print the final BBO / a checksum so nothing is optimised
away.

## 8. Profiling

`perf stat -r 5` over task-clock, cycles, instructions, branches,
branch-misses, cache-misses, L1-dcache-load-misses, dTLB-load-misses,
page-faults, plus `perf record` / `perf report`. If counters are unavailable on
your machine, report which ones and say so.

## 10. Results table

| Metric | A | B | C |
| :--- | :--- | :--- | :--- |
| Total time (ms) | | | |
| ns / add | | | |
| ns / cancel | | | |
| ns / BBO | | | |
| IPC | | | |
| Branch misses (%) | | | |
| L1-dcache-load-misses | | | |
| Cache misses | | | |
| Page faults | | | |

## 11. Performance analysis

1-2 pages: which is fastest for add/cancel/BBO and why; who has the most branch
misses and why; who has the most cache misses and why (contiguous vs pointer
chasing vs many small allocations); who has the most page faults and what that
says about allocation; which one you would deploy and why (latency,
predictability, memory, cache, complexity, maintainability); and what you would
try next.

## 12. Deliverables

Source (three implementations, benchmark, correctness test), README (build,
run, interface changes, data-structure choices), correctness output, `perf`
output, and the report with the results table and the analysis.

## 13. Grading

Correctness 35 · three genuinely different designs 20 · clean benchmark +
proper perf data 25 · performance analysis 15 · readability 5.
