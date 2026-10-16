# Rohan Ranjan-10428-Capstone project

name: "Rohan Ranjan"

email: "rohan.24bcs10428@sst.scaler.com"

roll_no: "10428"

A single-instrument order book written three times with three different data structures. One benchmark program runs any of them, `--verify` checks that all three give the same answers, and the results plus perf data are in [REPORT.md](REPORT.md).

## Files

| File | What it is |
|---|---|
| `orderbook.hpp` | API types (`Side`, `Level`, `BBO`), tick constants, price validation |
| `book_a_map.hpp` | Implementation A: `std::map` levels + `std::unordered_map` orders |
| `book_b_heap.hpp` | Implementation B: heaps with lazy deletion + `std::map` orders |
| `book_c_array.hpp` | Implementation C (my design): price array + bitmap + orders indexed by id |
| `workload.hpp` | random workload (fixed seed 42), built once before timing |
| `bench.cpp` | the one program: `--impl A/B/C` benchmark and `--verify` correctness test |
| `run_perf.sh` | `perf stat` + `perf record` for A, B, C (Linux) |
| `mac_counters.sh` | cycles / instructions / IPC / page faults on macOS (no perf there) |
| `cachegrind.sh` | branch and cache misses with valgrind cachegrind (simulated CPU) |
| `perf.Dockerfile` | Linux image with g++, perf and valgrind, to run the scripts from a Mac |
| `results/` | the raw output of every run used in the report |
| `REPORT.md` | results table, perf output and the performance analysis |

## Build

One command, standard library only:

```
g++ -std=c++20 -O2 bench.cpp -o bench
```

I use C++20 only for `std::countl_zero` / `std::countr_zero` from `<bit>` (bitmap search in C). Apple clang works the same way (`clang++ -std=c++20 -O2 bench.cpp -o bench`).

## Correctness test

```
./bench --verify
```

It runs the hand-written edge cases from the assignment (empty book, unknown cancel, double cancel, only 3 levels when 10 are asked for, invalid prices), then three random workloads: the real benchmark workload (1M ops) and two "thin book" workloads (500k ops each, 50% add / 40% cancel, prices close to the mid) where the best bid and ask empty all the time. After every single operation it compares the BBO, the top 10 bid levels and the top 10 ask levels of each book with a deliberately simple reference book (an array scanned tick by tick). Every 1000 ops it compares the whole book, and cancel return values are checked too.

Output on my machine:

```
Running correctness test...
(edge cases + benchmark workload (1000000 ops) + 2 thin-book workloads (500000 ops each).
 BBO and top 10 levels checked after every op, full book every 1000 ops,
 all compared against a simple reference book)

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

To be sure the test actually catches mistakes, I planted bugs one at a time (wrong next-best search in C, a bitmap mask off by one in C, stale heap levels shown in B, empty map levels not erased in A), and each one made its implementation FAIL. I also ran the test under AddressSanitizer + UBSan on Linux; it was clean.

## Benchmark

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Each run does 1,000,000 operations (about 70% add, 20% cancel, 10% `get_bbo`) plus `get_bids(10)` and `get_asks(10)` every 100 operations. Side is 50/50, qty 1-100, the price is a random valid tick in that side's band, and cancels always pick a live order. The workload is built before the clock starts, so all three books get exactly the same list of operations.

It runs 5 times and prints the median of: total time, ns per `add_order` / `cancel_order` / `get_bbo`, the time for the depth queries and p50/p99/p99.9/max per call. At the end it prints the final BBO and a checksum of every result, and all three books print the same checksum (`12797490362879580754`).

Extra flags: `--runs N`, `--ops N`, `--seed N`, `--total-only` (no per-call timers, used under perf), and `--impl none` (only builds the workload, as a baseline).

## perf

On a Linux machine:

```
./run_perf.sh          # writes results/linux/perf_stat_*.txt and perf_report_*.txt
```

My laptop is a Mac, so I ran the same script inside Docker (`perf.Dockerfile` just adds `g++` and `linux-perf` to `debian:bookworm`):

```
docker build -t orderbook-perf -f perf.Dockerfile .
docker run --rm --privileged -v "$PWD":/w -w /w orderbook-perf ./run_perf.sh results/linux
```

The Docker VM on Apple Silicon does not expose hardware counters, so there `perf stat` only gives `task-clock` and `page-faults` (the rest say `<not supported>`), and `perf record` samples with `cpu-clock`. To still get cycles, instructions and IPC from the real hardware I use macOS's own counters:

```
./mac_counters.sh      # uses /usr/bin/time -l, writes results/mac_counters.txt
```

For branch misses and cache misses I use valgrind's cachegrind, which runs the program on a simulated CPU (caches set to the M4's sizes), so it works without hardware counters:

```
docker run --rm -v "$PWD":/w -w /w orderbook-perf ./cachegrind.sh   # writes results/cachegrind/
```

The cachegrind numbers are simulated, so they are good for comparing A, B and C but are not exact hardware counts. Running `./run_perf.sh` on a lab Linux machine gives the real branch, cache, L1 and dTLB counters.

## Interface changes

The five functions keep exactly the signatures from the assignment (`double` price, `long` qty, `uint64_t` id). What I changed or decided:

- Inside every book prices are integer ticks (`99.99` becomes `9999`), converted once in `add_order`. Prices coming out of `get_bbo` / `get_bids` / `get_asks` are converted back with `tick / 100.0`.
- Invalid orders are ignored (no crash, nothing added): qty ≤ 0, a price that is not a multiple of 0.01, a bid outside [50.00, 99.99] or an ask outside [100.01, 150.00].
- `add_order` with an id that is still live is ignored (ids are supposed to be unique anyway).
- `get_bids` / `get_asks` with depth ≤ 0 return an empty vector.
- Each book has a `static name()` for printing. There is no base class with virtual functions; the benchmark is a template, so all three are called the same way and the calls can be inlined.

## Data structure choices

All three books use the same tick conversion and validation and nothing else. They differ in all four places the assignment asks about.

### A: `std::map` + `std::unordered_map`

1. Data structures: two `std::map<int, long>` (bids with `std::greater` so the highest price is first, asks lowest first) and one `std::unordered_map<uint64_t, OrderInfo>`.
2. Orders: hash table, id → {side, tick, qty}. One heap node per order.
3. Price levels: red-black tree per side, tick → total qty. A level is erased when its qty hits 0.
4. Best bid/ask: `begin()` of each map.
5. Cancel: hash lookup by id gives the tick, then `map.find(tick)`, subtract, erase if empty.

### B: heaps with lazy deletion + `std::map`

This is the "priority queue + lazy deletion" idea from class.

1. Data structures: `std::map<uint64_t, OrderInfo>` for orders, `std::unordered_map<int, long>` for level totals, and a binary heap of ticks per side (max-heap for bids, min-heap for asks), built with `std::push_heap` / `std::pop_heap` on a vector.
2. Orders: red-black tree keyed by id (no hashing).
3. Price levels: total qty in the hash map; each non-empty tick sits in its side's heap exactly once.
4. Best bid/ask: heap top. When a cancel empties a level, the tick stays in the heap with qty 0 ("stale"). Stale ticks are popped only when they reach the top, so the top is always live. Adding at a stale tick just revives it, with no second push.
5. Cancel: tree lookup by id, subtract from the level, clean the heap top if needed.

`get_bids` / `get_asks` walk the heap array best-first without changing it (a small priority queue of heap positions: take the best, push its two children), skipping stale ticks. That is why I used the heap functions on a vector: `std::priority_queue` hides the array.

### C: price array + bitmap + orders indexed by id (my design)

1. Data structures: `std::vector<long>` with one total per tick (10,001 slots, index = tick − 5000), a bitmap with one bit per tick (157 × 64-bit words), the cached best bid and best ask index, and a `std::vector<Slot>` indexed by order id.
2. Orders: ids come in increasing and are never reused, so the id is the index: `by_id_[id] = {tick, qty}`. The vector grows by doubling. Ids ≥ 16M go into a small `unordered_map` fallback so any `uint64_t` still works.
3. Price levels: the flat array, no tree and no hashing. The bit for a tick is set while its qty > 0.
4. Best bid/ask: stored, so `get_bbo` is two array reads. If a cancel empties the best level, the bitmap finds the next non-empty tick 64 ticks at a time with `countl_zero` / `countr_zero`. Bids are all below index 5000 and asks all above, so a search never crosses into the other side.
5. Cancel: `by_id_[id]` gives the tick directly, subtract from the array, clear the bit if the level is empty.

### Complexity

L = live price levels on a side (at most 5,000), N = live orders, d = requested depth, H = ticks in B's heap (live + stale, at most 5,000 per side).

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | O(log L) + O(1) avg hash | O(log N) tree + O(1) avg hash, + O(log H) only for a new level | O(1) (amortized, vector growth) |
| `cancel_order` | O(1) avg hash + O(log L) | O(log N) + amortized O(log H) for lazy pops | O(1); O(L/64) word scan if the best level empties |
| `get_bbo` | O(1) | O(1) (+ one hash lookup for qty) | O(1) |
| `get_bids` / `get_asks` | O(d) | O((d + s) log(d + s)), s = stale ticks passed | O(d + gap/64) |
