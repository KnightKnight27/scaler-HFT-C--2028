# Capstone: Low-Latency Order Book

- Name: Parth Sankhla
- Roll No: 24BCS10229
- Email: parth.24bcs10229@sst.scaler.com

One instrument, resting orders only (no matching), built three different ways and benchmarked on the same 1,000,000 op workload. The full analysis is in [REPORT.md](REPORT.md) and the raw outputs are in `results/`.

## Files

| file | what it is |
| --- | --- |
| `common.hpp` | `Side`, `Level`, `BBO`, `Order` (with a timestamp), the price band and `valid_price()` |
| `map_book.hpp` | Implementation A, `MapBook` |
| `heap_book.hpp` | Implementation B, `HeapBook` |
| `array_book.hpp` | Implementation C, `ArrayBook` (my own design) |
| `bench.cpp` | workload, benchmark (`--impl A/B/C`) and correctness test (`--verify`) |
| `Makefile` | build |
| `results/` | verify output, benchmark output, perf stat and perf report for each one |

## Data structures

### A: `MapBook`

- Price levels: `std::map<int, PriceLevel, std::greater<int>>` for bids (highest first) and `std::map<int, PriceLevel>` for asks (lowest first). A `PriceLevel` is a `std::vector<Order>` plus the total qty, so this is the `map<PRICE, vector<Order>>` approach from class.
- Orders: each `Order` has an id, side, price, qty and a timestamp (`ts`, a counter that goes up with every add). New orders are pushed to the back of their level's vector, so inside a level they stay in arrival order (price time priority). There is also a `std::unordered_map<uint64_t, Order>` from id to order, so cancel knows which level to look in.
- Best bid / ask: `begin()` of each map.
- Cancel: find the id in the hash map, `find` the price in the map, search that level's vector for the id and erase it (the rest keep their order), subtract the qty, and erase the level if it has no orders left.

### B: `HeapBook`

- Price levels: `std::priority_queue<int>` of bid prices (max heap) and a min heap for ask prices. The qty for each price is in a `std::unordered_map<int, long>`.
- Orders: `std::map<uint64_t, Order>` (a tree this time, not a hash map).
- Best bid / ask: `top()` of each heap.
- Cancel: find the id in the order map, subtract from the qty map. A heap can't remove from the middle, so an empty level's price stays in the heap and gets popped later when it reaches the top (lazy delete). After a cancel empties a level, stale prices are popped off the top, so `top()` is always a live level.
- `get_bids` / `get_asks` copy the heap and pop from the copy, skipping stale and duplicate prices.

### C: `ArrayBook` (own design)

- Price levels: one `std::vector<long>` with a slot for every tick, `level[price]` = total qty. Only 10,000 prices are possible so this is small (about 120 KB).
- Orders: `std::vector<Order>` indexed directly by order id, qty 0 means not live. Ids are unique and come from a counter, so this is just an array lookup with no hashing.
- Best bid / ask: kept in two ints and updated on add. On cancel, if the best level becomes empty, walk the array to the next non-empty price.
- Cancel: `orders[id]`, subtract from `level[price]`, move the best price only if needed.

## Price representation and interface changes

The tick is 0.01, so prices are whole numbers of ticks (`int`) instead of `double`. 98.56 is `9856`, 100.01 is `10001`. Prices compare exactly and all three books agree without any rounding issues. Prices are turned back into `98.56` only when printing.

| | assignment | mine |
| --- | --- | --- |
| `add_order` | `(uint64_t id, Side side, double price, long qty)` | `(uint64_t id, Side side, int price, long qty)` |
| `Level::price` | `double` | `int` (ticks) |
| `BBO::bid_price`, `BBO::ask_price` | `double` | `int` (ticks) |

Other behaviour:

- `add_order` ignores the order if qty is 0 or less, if the price is outside its band (bids 5000 to 9999, asks 10001 to 15000) or if the id is already live.
- `cancel_order` returns `false` for an unknown or already cancelled id and doesn't change the book.
- `get_bbo()` on an empty side gives `has_bid` / `has_ask` = `false` with price and qty 0.
- `get_bids(depth)` / `get_asks(depth)` return fewer levels if the book doesn't have `depth` of them.
- `ArrayBook` grows its order vector to the biggest id it has seen, so it expects ids to come from a counter (like an exchange order id). It would waste memory with random 64 bit ids.

## Build

```
make
```

or directly:

```
g++ -std=c++17 -O2 -Wall -Wextra -g -fno-omit-frame-pointer -o bench bench.cpp
```

(`-g -fno-omit-frame-pointer` are only there so `perf record -g` gets proper call stacks.)

## Correctness test

```
./bench --verify
```

It first runs the small cases from the assignment on each book (empty book, cancelling an unknown id, cancelling twice, asking for 10 levels when there are 3, bad prices, the best price moving after a cancel). Then it runs the same 1,000,000 op workload through all three books together and after every single op compares the BBO, the top 10 bids and the top 10 asks (prices and qty), and the return value of every cancel.

```
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## Benchmark

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Workload: 1,000,000 ops with `std::mt19937 rng(42)`, about 70% add, 20% cancel, 10% `get_bbo`, plus `get_bids(10)` and `get_asks(10)` every 100 ops. Side is 50/50, qty is 1 to 100, price is a random tick in that side's band, and a cancel always picks an order that is live. The workload is generated once before timing, so all three books get exactly the same ops.

Each op is timed with `std::chrono::steady_clock`, the program does 5 runs and prints the median. The final BBO and a checksum are printed at the end so the compiler can't throw the work away.

## perf

```
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

perf record -g ./bench --impl A
perf report
```

Same for B and C. Outputs are in `results/perf_stat_*.txt` and `results/perf_report_*.txt`.
