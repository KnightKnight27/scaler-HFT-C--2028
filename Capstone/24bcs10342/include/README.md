# Low-Latency Order Book (C++17)

**Name:** Nathan Joseph Diniz  **Roll number:** 24bcs10342  **SST email:** <your SST email>
**Team (max 2):** <partner name + SST email, or "solo">

## Build

```bash
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -o bench src/bench.cpp
# or
cmake -S . -B build && cmake --build build   # binary: build/bench
```

## Correctness test

```bash
./bench --verify
```
Runs 200,000 seeded random operations (adds, valid cancels, cancels of unknown/already-cancelled ids, BBO queries)
through A, B and C in lock-step, and after every operation compares add/cancel return values, `get_bbo()`,
`get_bids(10)`, `get_asks(10)` (and the full book every 1000 ops). It also runs edge-case tests (empty book,
unknown cancel, double cancel, insufficient depth, price-band checks, duplicate ids).

## Benchmark

```bash
./bench --impl A      # also B, C
```
Pre-generates 1,000,000 ops (seed 42; 70% add / 20% cancel / 10% BBO; `get_bids(10)` + `get_asks(10)` every 100 ops;
50/50 side, qty 1–100, random valid tick in the side's band, cancels always target a live order). The identical
stream is used for every implementation. Runs 5 times and reports the median. Each run does:
1. a clean pass (no per-call timers) → **total time**
2. an instrumented pass with `steady_clock::now()` around each call → **ns/add, ns/cancel, ns/BBO**
   (the measured cost of one `now()` pair is subtracted and printed).

A checksum of every result is printed so the work cannot be optimised away; it is identical across A/B/C.

Options: `--ops N`, `--seed S`, `--runs R`, `--no-breakdown`.

### perf
```bash
for i in A B C; do
  perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
    ./bench --impl $i --runs 1 --no-breakdown
done
perf record -g ./bench --impl A --runs 1 --no-breakdown && perf report     # repeat for B, C
```
`--runs 1 --no-breakdown` keeps perf focused on one clean pass. Workload generation (~24 MB of ops) is common
to all implementations and contributes equally to page faults.

## Interface changes
- Prices are **integer ticks** (`10001` = 100.01) instead of `double`; `to_ticks()/to_price()/fmt_price()` helpers are in `include/types.hpp`.
- `add_order` returns `bool` (false if qty ≤ 0, price outside its band, or duplicate id). The book is unchanged on rejection.
- Depth `Level.price`, `BBO.*_price` are ticks as well.

## Data-structure choices

| | A | B | C |
|---|---|---|---|
| Price levels | `std::map<tick, qty>` (red-black tree) | sorted `std::vector<Level>`, best at back | flat `long qty[10001]` indexed by tick |
| Orders | `std::unordered_map<id, Order>` | `std::vector<Order>` sorted by id, tombstoned on cancel | slot pool + free list |
| Best bid/ask | `begin()` of map | `back()` of vector | cached `best_bid_/best_ask_`, rescan only if best level empties |
| Find order on cancel | hash lookup | binary search on id | custom open-addressing hash (backward-shift delete) → slot |

L = number of non-empty price levels (≤ 5,000 per side), N = orders.

| Operation | A | B | C |
|---|---|---|---|
| add_order | O(log L) avg + O(1) hash | O(log L + L) shift; order append O(1) if ids increase (O(N) otherwise) | O(1) avg |
| cancel_order | O(log L) avg | O(log N + log L + L) shift | O(1) avg; O(L) worst case on best-level rescan |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids / get_asks | O(depth) | O(depth) | O(depth + empty ticks skipped), worst O(L) |

## Correctness results

Output of `./bench --verify` on my machine (saved in `results/verify.txt`):

```
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
(200000 operations incl. invalid cancels, 781219 comparisons: BBO, depth-10 bids/asks, full-book, cancel/add results)

All implementations produce identical results.
```

The 1M-op benchmark also prints the same final BBO (`99.99 x 2015` / `100.01 x 2522`) and the same checksum (`2504230024183926013`) for A, B and C.

## Performance results

**Environment:** WSL2 (Ubuntu 22.04, kernel 5.15) on Windows, `g++ 11.2 -O2`, `perf` 5.15.0-198. All nine requested counters were available.
Because 9 counters were requested, the PMU time-multiplexed them, so each counter was only active ~85% of the time (the `(85.xx%)` column); perf scales the values up. Treat small differences as noise.

Raw outputs are in `results/` (`perf_stat_A/B/C.txt`, `bench_A/B/C.txt`, `verify.txt`).

### perf stat (`perf stat -r 5 -e ... ./bench --impl X --runs 1 --no-breakdown`)

**A**
```
            697.00 msec task-clock:u              #    0.943 CPUs utilized            ( +-  1.59% )
        1000913324      cycles:u                  #    1.374 GHz                      ( +-  0.55% )  (85.76%)
         740815484      instructions:u            #    0.74  insn per cycle           ( +-  0.12% )  (85.73%)
         135985126      branches:u                #  186.719 M/sec                    ( +-  0.33% )  (85.73%)
          10343768      branch-misses:u           #    7.54% of all branches          ( +-  0.34% )  (85.69%)
           9590431      cache-misses:u                                                ( +-  1.12% )  (85.71%)
          19040412      L1-dcache-load-misses:u                                       ( +-  0.55% )  (85.71%)
           1003948      dTLB-load-misses:u                                            ( +-  1.87% )  (85.66%)
             15379      page-faults:u             #   21.117 K/sec                    ( +-  0.00% )
            0.7395 +- 0.0116 seconds time elapsed  ( +-  1.56% )
```

**B**
```
            598.45 msec task-clock:u              #    1.066 CPUs utilized            ( +-  2.58% )
         739446860      cycles:u                  #    1.325 GHz                      ( +-  1.42% )  (84.54%)
         536505730      instructions:u            #    0.75  insn per cycle           ( +-  0.37% )  (85.50%)
          78517240      branches:u                #  140.710 M/sec                    ( +-  0.11% )  (86.03%)
          11749424      branch-misses:u           #   14.98% of all branches          ( +-  0.36% )  (85.28%)
          21042089      cache-misses:u                                                ( +-  0.80% )  (86.32%)
          37473087      L1-dcache-load-misses:u                                       ( +-  0.53% )  (86.50%)
           1527532      dTLB-load-misses:u                                            ( +-  2.41% )  (85.83%)
             13525      page-faults:u             #   24.238 K/sec                    ( +-  0.00% )
            0.5615 +- 0.0143 seconds time elapsed  ( +-  2.55% )
```

**C**
```
            397.25 msec task-clock:u              #    0.957 CPUs utilized            ( +-  3.60% )
         381173382      cycles:u                  #    0.927 GHz                      ( +-  2.26% )  (85.23%)
         429267129      instructions:u            #    1.12  insn per cycle           ( +-  0.89% )  (85.76%)
          54478204      branches:u                #  132.499 M/sec                    ( +-  0.63% )  (85.83%)
           3565654      branch-misses:u           #    6.57% of all branches          ( +-  0.50% )  (85.80%)
           2570117      cache-misses:u                                                ( +-  1.54% )  (85.83%)
           5480858      L1-dcache-load-misses:u                                       ( +-  1.67% )  (85.79%)
            810594      dTLB-load-misses:u                                            ( +-  1.66% )  (85.76%)
             21538      page-faults:u             #   52.384 K/sec                    ( +-  0.00% )
            0.4149 +- 0.0140 seconds time elapsed  ( +-  3.38% )
```

### Results table

Timing rows are the median of 5 runs (`./bench --impl X`). The perf rows come from `perf stat -r 5` (mean of 5 processes), each doing one clean pass.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 426.64 | 350.12 | **139.70** |
| ns / add | 380.3 | **105.0** | 197.2 |
| ns / cancel | 722.9 | 555.0 | **249.2** |
| ns / BBO | 1.2 | ~0 (below timer resolution) | 2.4 |
| IPC | 0.74 | 0.75 | **1.12** |
| Branch misses (%) | 7.54 | 14.98 | **6.57** |
| L1-dcache-load-misses | 19,040,412 | 37,473,087 | **5,480,858** |
| Cache misses | 9,590,431 | 21,042,089 | **2,570,117** |
| Page faults | 15,379 | **13,525** | 21,538 |

**Measurement caveats.**
- The per-op times use `steady_clock::now()` around every call, minus the measured cost of one `now()` pair. That overhead was 54.4 ns (A), 57.3 ns (B) and 29.5 ns (C) in the three runs, so it varies. `get_bbo` takes only 1–3 ns, which is far below what this method can resolve (B came out at 0.0 after the clamp), so I do not claim a BBO winner. A better method would time a block of 1000 BBO calls at once.
- perf's `task-clock` (400–700 ms) is larger than the "Total time" row because perf also counts process start-up and generating the 1M-op workload (about 24 MB, which causes roughly 6k page faults for every implementation).
- WSL2 is a virtualised environment, so run-to-run noise is higher than on bare metal (A's total time varied between 476 and 542 ms across runs).
- The explanations below come from reading the code together with these counters. I have not yet confirmed them with `perf record`.

## Performance analysis

### 11.1 Which implementation is fastest?
**Overall: C** (139.7 ms vs 350 ms for B and 427 ms for A).
- **add_order: B is fastest** (105 ns), then C (197 ns), then A (380 ns). B appends new orders to the back of a vector, because ids increase, so it writes sequentially with no hashing. The level insert is a binary search over at most ~5,000 small `Level` structs (about 80 KB), which stays in cache. A pays for heap-allocating an `unordered_map` node per order, plus a `std::map` node for each new level. C does one probe into a large, cold hash table (random access, so a likely cache miss), and its table doubles several times as orders accumulate, which makes some adds expensive.
- **cancel_order: C is fastest** (249 ns), then B (555 ns), then A (723 ns). C needs one hash probe, one read of the order slot and one array update. B does a binary search over a sorted vector of ~700k orders (about 20 dependent steps, the last ones on cold cache lines) and then erases from the level vector, which shifts elements. A does a hash lookup, a tree walk to the level and a node free.
- **get_bbo: no clear winner.** All three are O(1) and measure 0–2.4 ns, which is below the resolution of my timer method (see caveats).

### 11.2 Branch prediction
**B has the most branch misses (14.98%, 11.7M misses).** `std::lower_bound` over the order vector and the level vector makes a data-dependent comparison at every step, and for random ids and prices the outcome is close to 50/50, so the predictor cannot learn it. `vector::insert/erase` also adds branches in the element-shifting loops. A (7.54%) has similar data-dependent decisions while descending the red-black tree and walking hash chains, but fewer of them. C has the lowest rate (6.57%, 3.6M misses). What remains comes from the probe loops in the hash table, the best-level rescan loops, and the random 70/20/10 op-type and buy/sell branches in the benchmark loop itself, which every implementation shares.

### 11.3 Cache behaviour
**B has the most cache misses (21.0M cache-misses, 37.5M L1 misses), then A (9.6M / 19.0M), and C has the fewest (2.6M / 5.5M).**
- **B** uses contiguous memory, but the binary search over the order vector "jumps around" it: each lookup touches ~20 positions, and the last several are on cold lines. Tombstoned orders are never removed, so the vector keeps growing (to ~700k entries x 24 bytes ≈ 17 MB) and exceeds the L2 cache.
- **A** follows pointers: every `std::map` and `std::unordered_map` node is a separate small heap allocation, so a lookup chases several pointers across scattered memory.
- **C** accesses flat arrays: the level array (10,001 x 8 bytes ≈ 80 KB) fits in L2, and each operation needs just one hash probe plus one slot access. It has no per-order allocation.
- Each L1 miss costs roughly 10–15 cycles when served from L2, and more when it goes to L3 or DRAM. That is consistent with C's much higher IPC (1.12 vs ~0.75).

### 11.4 Page faults
**C has the most page faults (21,538), vs 15,379 for A and 13,525 for B.** About 6k of each total is the shared workload array. C's extra faults are consistent with its growth strategy: the id hash table doubles repeatedly (from 65,536 entries to about a million entries x 16 bytes ≈ 16 MB) and every new table is fresh, zeroed memory whose pages are touched for the first time, and the slot pool also grows by reallocation. A's nodes come from the malloc heap, which reuses memory freed by cancels. B reserves its order vector once and only touches pages as it fills them. This tells us C front-loads memory work during growth, and that the cost could be moved out of the hot path by pre-sizing everything at start-up.

### 11.5 Production choice
**I would deploy C**, with some caveats.
- **Latency:** it has the lowest total time and the fastest cancel (about 3x faster than A), and the highest IPC.
- **Predictability:** its work per operation is O(1) on average, with no per-order allocation. There are two risks, though: the hash-table doubling causes a latency spike when it rehashes, and cancelling the best level triggers a rescan of up to ~5,000 ticks. I measured only averages, so before deploying I would measure p99 and p99.9 latencies. Both risks can be reduced (see 11.6).
- **Memory:** the level array is fixed at ~80 KB and everything else scales with live orders. B is unattractive because its tombstones are never reclaimed and an out-of-order id costs an O(N) insert.
- **Cache behaviour:** best of the three, by a wide margin.
- **Complexity and maintainability:** C is the hardest to get right, because it includes a hand-written hash table with backward-shift deletion. A is the simplest and easiest to maintain. I would still choose C for the hot path, and keep A as a reference oracle in `--verify` so the custom code is continuously checked against the simple implementation.

### 11.6 What I would try next
1. Pre-size the hash table and slot pool (`reserve`) and pre-touch the pages at start-up, which removes the rehash spikes and moves page faults out of the hot path.
2. Replace the best-level rescan with a bitmap (or two-level bitmap) of non-empty ticks, using find-first-set to locate the next level in O(1).
3. If order ids are dense or bounded, replace the hash table with a direct-indexed array.
4. Shrink the `Slot` (e.g. 16 bytes, 32-bit quantity) and add prefetching on the hash probe.
5. Use huge pages to reduce dTLB misses (0.8M–1.5M here).
6. Compile with PGO to improve branch layout and reduce mispredictions.
7. Improve the benchmark: time batches of BBO calls, report p50/p99/p99.9 per operation, and run on bare-metal Linux.
8. Use `perf record -g` to confirm where time goes in each implementation, rather than relying on counters alone.
