# Capstone: Low-Latency Order Book

**Varun Reddy** · varun.24bcs10009@sst.scaler.com · Roll no. **24BCS10009**  
**Aniruddha Patil** · Aniruddha.23bcs10156@sst.scaler.com · Roll no. **23BCS10156**

A resting order book for one instrument (no matching), implemented three times
with genuinely different data structures, checked against a reference model,
benchmarked with one program, and profiled with Linux `perf`.

- **[REPORT.md](REPORT.md)**: results table, perf output and the performance analysis.
- **[results/](results/)**: raw output of every command used in the report.

## Layout

```
src/common.hpp         Side, Level, BBO, price <-> tick helpers, shared order validation
src/book_map.*         Implementation A: std::map levels + std::unordered_map orders
src/book_vector.*      Implementation B: sorted std::vector levels + id-sorted std::vector orders
src/book_array.*       Implementation C: tick-indexed array + bitmap + open-addressing order table
src/tick_bitmap.hpp    C's two-level occupancy bitmap (O(1) "next non-empty price")
src/order_table.hpp    C's linear-probing hash table (16-byte slots, backward-shift delete)
src/workload.*         deterministic workload generators (benchmark + stress)
src/driver.hpp         runs one operation against any book, keeps a checksum
src/bench.cpp          the single benchmark program (--impl / --verify)
src/verify.cpp         correctness test
scripts/run_all.sh     reproduces everything in results/
```

## Build

Requires g++ with C++17 (tested with g++ 13.3, Ubuntu 24.04). Standard library only.

```bash
cmake -S . -B build && cmake --build build -j      # -> build/bench
```

or with a single command:

```bash
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra src/*.cpp -o bench
```

`-O2` is the optimisation level. `-g` and `-fno-omit-frame-pointer` only add debug
symbols and keep the frame pointer so that `perf record -g` call graphs work.

## Correctness test

```bash
./build/bench --verify
```

Three stages run, and every implementation is compared with a deliberately simple
reference model (orders in a `std::map`, quantities in a plain array, queries
answered by scanning):

1. **Edge cases** (31 checks): empty book, cancelling unknown and already-cancelled ids,
   insufficient depth, several orders at one price, best price moving after a cancel,
   invalid prices and quantities, duplicate ids, band-edge prices.
2. **Benchmark workload** (the exact 1M-op workload used by `--impl`) is replayed in
   lockstep through A, B, C and the reference. After **every** operation the check
   compares the return value, the BBO, `get_bids(10)` and `get_asks(10)`. Every 1,000
   operations it also compares the full depth of both sides and the live order count.
3. **Stress workload**: random 64-bit (non-monotonic) ids, prices clustered at the inside
   so levels empty constantly, cancels of unknown and dead ids, invalid orders, random
   depths, and periodic full drains to an empty book.

```
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

Full output: [results/verify.txt](results/verify.txt). To confirm the test can fail,
four bugs were planted one at a time (A never erasing empty levels, B allowing a
double cancel, C not moving the best ask, C deleting from the hash table without
backward shift). `--verify` reported FAIL for the right implementation every time.

## Benchmark

```bash
./build/bench --impl A        # or B, or C
```

The same program and the same workload serve all three implementations. Options:

| option | meaning |
|---|---|
| `--impl A\|B\|C` | implementation to benchmark (`none` = only generate the workload, used as a perf baseline) |
| `--reps N` | repetitions, median reported (default 5) |
| `--ops N` | core operations (default 1,000,000) |
| `--seed S` | RNG seed (default 42, `std::mt19937`) |
| `--timer tsc\|steady` | per-op timer (see below) |
| `--no-latency` | total time only: skip the per-op timed pass and the BBO loop (used under perf) |

**Workload** (generated before any timing): 1,000,000 operations (70.0% add, 19.9% cancel,
10.0% BBO), plus `get_bids(10)` and `get_asks(10)` after every 100 operations. Side is
50/50, quantity is 1–100, and each price is a uniform valid tick in its side's band.
Every cancel picks a random order that is currently live. Ids are 1, 2, 3, …

**Each repetition** runs two passes on a fresh book:
1. A *throughput pass* with no per-operation timers. It gives the total time
   (`std::chrono::steady_clock`), page faults and heap size, followed by 10M
   back-to-back `get_bbo()` calls to measure BBO read cost.
2. A *latency pass* that times every operation. It gives the average ns per add,
   cancel, BBO and depth query, and the p50/p90/p99/p99.9/max distribution.

The program prints the final BBO and a checksum over every result returned. The
checksum keeps the work from being optimised away, and it is identical for A, B
and C (`4d916b0a83d36be5`).

**Why a TSC timer?** On the machine used, the kernel had marked the TSC unstable and
fallen back to the HPET clocksource. Each `steady_clock::now()` was therefore a syscall
costing about **1.4 µs**, longer than the operations being timed. Total time still
uses `steady_clock`. Per-op timing reads the CPU's invariant TSC (`rdtsc` with
`lfence`, about 20 ns per pair) and converts ticks to ns with a `steady_clock`
calibration. The empty-timer cost is measured and subtracted. `--timer steady` uses
`steady_clock` instead ([results/bench_C_steady_timer.txt](results/bench_C_steady_timer.txt)).

### perf

```bash
sudo sysctl kernel.perf_event_paranoid=1       # once, if perf reports "No supported events"
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
    ./build/bench --impl A --reps 1 --no-latency
perf record -g ./build/bench --impl A --reps 1 --no-latency && perf report
```

Under perf the program runs one throughput pass per process; `-r 5` supplies the five
repetitions. Without `--no-latency`, the counters would be dominated by the timer and
BBO-loop code. `./scripts/run_all.sh` runs everything (verify, benchmarks, perf stat,
perf record) and writes `results/`.

## Interface changes

The public API is the one in the brief, with two small changes:

1. **`add_order` returns `bool`** instead of `void`. It returns `false`, leaving the book
   unchanged, if the price is not a multiple of 0.01, the price is outside the side's band
   (bids 50.00–99.99, asks 100.01–150.00), `qty <= 0` or `qty > 2^31-1`, or the id is
   already live. Callers that ignore the result are unaffected.
2. **`size_t order_count() const`** is added (number of live orders); verify uses it.

Prices are still `double` at the API boundary, but each book converts them to integer
ticks (`99.99 → 9999`) on entry and back on output, so no book stores, compares or sums
floating-point prices. All three books share one validation function, so they accept
and reject exactly the same orders. `get_bids`/`get_asks` with `depth <= 0` return an
empty vector.

## Data-structure choices

| | A: `std::map` + `std::unordered_map` | B: sorted `std::vector`s | C: own design |
|---|---|---|---|
| **Orders stored** | `unordered_map<id, {side,tick,qty}>`: chained hash, one heap node per order | `vector<{id,tick,side,qty}>` sorted by id; push_back for increasing ids; cancel leaves a tombstone, compacted when >50% dead | open-addressing hash table of 16-byte slots, pre-sized (2^20 slots), Fibonacci hash, linear probing |
| **Levels stored** | `map<tick, qty>` per side (red-black tree; bids `greater<>`, asks `less<>`) | `vector<{tick,qty}>` per side sorted worst → best | `array<int64, 10001>`: qty indexed by `tick - 5000`, plus a 2-level occupancy bitmap |
| **Best price** | `map.begin()` (cached leftmost node) | `vector.back()` | cached `best_bid_`/`best_ask_` index; bitmap search only when the best level empties |
| **Cancel lookup** | hash lookup, then tree lookup of the level | binary search by id, then binary search of the level | one hash probe; the slot holds the tick index → direct array access |

Big-O (N = live orders, L = price levels on a side ≤ 5,000, d = depth requested):

| op | A | B | C |
|---|---|---|---|
| add_order | O(log L) + O(1) avg | O(log L) + O(1) amortised (increasing ids); + O(L) if the level is new; O(N) for an out-of-order id | O(1) avg |
| cancel_order | O(1) avg + O(log L) | O(log N) + O(log L); + O(L) if the level empties | O(1) avg |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids / get_asks | O(d) | O(d) | O(d) |

See [REPORT.md](REPORT.md) for the measurements and analysis.
