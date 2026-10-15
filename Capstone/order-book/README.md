# Capstone: Low-Latency Order Book

- **Name:** Anuska Roy
- **Email:** anuska.24bcs10021@sst.scaler.com
- **Roll no:** 10021

A resting (no matching) order book for a single instrument, implemented three times
with genuinely different data structures, plus one shared benchmark/verification
program. The performance results and analysis are in **[REPORT.md](REPORT.md)**.

```
order-book/
├── src/
│   ├── common.hpp        Side / Level / BBO, tick helpers, input validation
│   ├── order_book_a.hpp  A: std::map levels + std::list FIFO + unordered_map index
│   ├── order_book_b.hpp  B: priority_queue (lazy deletion) + unordered_maps
│   ├── order_book_c.hpp  C: price-indexed array + bitmap + order pool + open addressing
│   └── bench.cpp         benchmark (--impl A|B|C) and correctness test (--verify)
├── scripts/
│   ├── run_macos.sh      build + verify + bench + /usr/bin/time -l counters (macOS)
│   ├── run_perf.sh       build + verify + perf stat + perf record (Linux)
│   ├── docker_perf.sh    runs run_perf.sh in a Debian container (from macOS)
│   └── cachegrind.sh     simulated cache/branch counters via Valgrind (Docker)
├── results/              raw output of every run quoted in REPORT.md
└── CMakeLists.txt
```

## Build

A single command (C++17, `-O2`, standard library only):

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o bench src/bench.cpp
```

or with CMake (`-O2 -g -fno-omit-frame-pointer`, for `perf record` call graphs):

```bash
cmake -S . -B build && cmake --build build && ./build/bench --verify
```

## Run the correctness test

```bash
./bench --verify
```

It runs:
1. **Edge cases** against each implementation: empty book, cancelling an unknown or
   already-cancelled id (must return `false`), insufficient depth, summed quantity
   per level, best price moving after the best level empties, and rejection of
   invalid input.
2. **Random cross-check.** Three seeded workloads (the 1M-op benchmark mix, a
   20-tick narrow band where the best levels empty and refill constantly, and a
   cancel-heavy mix that keeps draining the book to empty) go through A, B, C and a
   deliberately simple reference model (`std::map<tick, qty>`). After **every**
   operation it compares the `cancel_order` return value (and checks that a second
   cancel returns `false`), `get_bbo()`, and `get_bids(10)` / `get_asks(10)`, and
   every 1000 ops it compares the **full** depth of both sides.

Output (also in [results/macos/verify.txt](results/macos/verify.txt)):

```
Running correctness test...
  edge cases (empty book, unknown/double cancel, depth, invalid input): done
  workload: benchmark mix 70/20/10, full price band  1000000 ops, seed 42: done
  workload: narrow band (20 ticks/side)              1000000 ops, seed 43: done
  workload: cancel-heavy 50/45/5, 50 ticks/side      200000 ops, seed 44: done

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

The exit code is 0 on success and 1 on any mismatch. To check that the test can
actually fail, I planted a bug in a scratch copy of C (the best-bid rescan skipping
one level). It reported `Implementation C: FAIL (op 11: BBO mismatch)`.

The benchmark also prints a checksum over every query result. All three
implementations produce the same checksum (`17644364262346812077`) on macOS and on
Linux.

## Run the benchmark

One program, implementation chosen on the command line:

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

| Flag | Default | Meaning |
|---|---|---|
| `--impl A\|B\|C` | – | which order book to benchmark |
| `--impl 0` | – | baseline: only generate the workload (to subtract from whole-process counters) |
| `--runs N` | 5 | repeat on a fresh book N times; per-run results and the **median** are printed |
| `--ops N` | 1,000,000 | operations per run |
| `--seed S` | 42 | `std::mt19937` seed |
| `--untimed` | off | compile out the per-op timers and measure only total time (used for `perf record`) |

Workload, as the spec requires: 1,000,000 ops, about 70% add / 20% cancel / 10% BBO,
plus `get_bids(10)` and `get_asks(10)` every 100 ops. Sides are 50/50, qty is 1–100,
and prices are uniform over the valid ticks for the side. A cancel always targets a
live order. The whole operation stream is generated **before** timing, so RNG cost is
never measured and every implementation gets the same input. Each operation is timed
on its own with `std::chrono::steady_clock`. The program prints the final BBO and a
checksum so the compiler cannot remove the work.

Profiling:

```bash
./scripts/run_perf.sh      # on a Linux machine: perf stat -r 5 (all spec counters) + perf record -g
./scripts/docker_perf.sh   # same, inside Docker (from macOS)
./scripts/run_macos.sh     # macOS: bench + instructions/cycles/page faults via /usr/bin/time -l
./scripts/cachegrind.sh    # simulated L1/LL cache misses + branch mispredictions
```

## Interface changes

The required API is kept exactly:

```cpp
void add_order(uint64_t id, Side side, double price, long qty);
bool cancel_order(uint64_t id);
BBO get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

Changes and additions:
- **Integer ticks inside.** `add_order` turns the `double` price into an integer
  tick once (`99.99 → 9999`, `100.01 → 10001`), and every data structure stores ticks.
  Prices convert back to `double` only when a `Level`/`BBO` is returned. So there is
  no floating-point comparison or rounding inside the books.
- **Invalid input is ignored** (`add_order` returns `void`, so it is a no-op), the same
  way in all three implementations: qty ≤ 0, a price not on the 0.01 grid, a bid
  outside [50.00, 99.99], an ask outside [100.01, 150.00], or a duplicate order id.
- Added `operator==` for `Level` and `BBO` (used by the verifier).
- The three classes are `OrderBookA`, `OrderBookB` and `OrderBookC`. They have the
  same method signatures, and the benchmark drives them through templates
  (Hint 5: one logical interface, no virtual-call overhead).

## Data-structure choices

| | A — tree | B — heap | C — own design |
|---|---|---|---|
| **Orders stored in** | `std::list<Order>` per level (one heap node per order, FIFO) | `unordered_map<id, {side, tick, qty}>` (one hash node per order) | one contiguous `std::vector<Order>` pool, slots reused via a free list, linked into per-level FIFO lists by 32-bit indices |
| **Price levels stored in** | `std::map<tick, Level>` (red-black tree), `greater<>` for bids | `unordered_map<tick, qty>` + `priority_queue<tick>` (max-heap bids, min-heap asks) | flat array of all 10,001 ticks (`{total, head, tail}`, 16 B each) + 157-word occupancy bitmap |
| **Best bid/ask found by** | `map.begin()` | `heap.top()`; cancels pop stale (empty) prices off the top right away, so the top is always live | cached `best_bid_`/`best_ask_` index; when the best level empties, scan the bitmap 64 levels per step with `clz`/`ctz` |
| **Order found for cancel by** | `unordered_map<id, {side, tick, list iterator}>` then `map.find(tick)` | `unordered_map<id, OrderInfo>` | open-addressing hash table `id → pool slot` (linear probing, Fibonacci hashing, backward-shift delete, no tombstones) |
| **Time priority** | list order | not stored (no matching, so it is not observable) | intrusive list order |

### Complexity

P = live price levels on a side (≤ 5,000), H = prices in the heap including stale
ones (≤ 5,000 per side), L = 10,001 tick slots, d = requested depth. Hash operations
are average O(1).

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | O(log P) | O(1), plus O(log H) when a new level is created | O(1) amortized |
| `cancel_order` | O(log P) (`map.find`) + O(1) | O(1) + amortized O(log H) lazy pruning | O(1); O(L/64) ≤ 157 word scan only if the best level empties |
| `get_bbo` | O(1) | O(1) | O(1) |
| `get_bids` / `get_asks` | O(d) | O(H + d·log H) (copy the heap, pop) | O(d + L/64) |

More detail on each design is in the header comments of the source files and in
[REPORT.md](REPORT.md).
