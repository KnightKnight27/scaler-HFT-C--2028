# Capstone: Low-Latency Order Book - Subhan Rahiman

## Submission Details

```yaml
name: "Subhan Rahiman"
email: "subhan.24bcs10095@sst.scaler.com"
roll_no: "24BCS10095"
team: "solo"
```

## Summary

A single-instrument resting order book (no matching), implemented **three genuinely different ways**, with one
benchmark/verification program (`bench`), `perf` data, a results table and a performance analysis.

| | Implementation A | Implementation B | Implementation C (my own design) |
|---|---|---|---|
| Idea | `std::map` + `std::unordered_map` | sorted `std::vector`s + id-indexed table | price-indexed arrays + bitmap + custom hash |
| Total time, 1M ops (macOS, median of 5) | 88.2 ms | 40.5 ms | **15.7 ms** |

**Two honest limitations up front** (details in section 8):
1. The hardware counters the spec asks for (`cycles`, `instructions`/IPC, `branch-misses`, `cache-misses`,
   `L1-dcache-load-misses`, `dTLB-load-misses`) are **`<not supported>`** in the only Linux environment I had
   (Docker VM on an Apple M3 Pro). I report the counters that work (`task-clock`, `page-faults`) plus
   `perf record` function profiles, and I clearly mark every branch/cache explanation as a *hypothesis*, not a
   measurement. `scripts/run_perf.sh` regenerates everything in one command on any Linux machine that has a PMU.
2. Timings are **mean** latencies. I did not measure tail latency (p99/p99.9).

## Files

| File | Purpose |
|---|---|
| `common.hpp` | types, constants, tick helpers (`Price`, `Side`, `Level`, `BBO`) |
| `book_a.hpp` / `book_b.hpp` / `book_c.hpp` | the three implementations |
| `workload.hpp` | deterministic workload (fixed seed 42) shared by every implementation |
| `bench.cpp` | the single program: `--verify`, `--impl A|B|C|all`, `--perf` |
| `scripts/run_perf.sh` | runs all `perf stat` / `perf record` collection (Linux) |
| `results/` | raw outputs quoted in this README |
| `Makefile` | `make`, `make verify`, `make run`, `make perf` |

## 1. Build and run

One command (C++17, `-O2`, standard library only):

```bash
g++ -O2 -std=c++17 bench.cpp -o bench
```

```bash
./bench --verify                 # correctness test (about 45 s)
./bench --impl A                 # benchmark one implementation (5 runs, median)
./bench --impl B
./bench --impl C
./bench --impl all               # all three + comparison table + checksum check
./bench --impl all --workload clustered   # extra workload, see 11.1
```

Other options: `--ops N` (default 1,000,000), `--seed S` (default 42), `--runs N` (default 5).
`--impl X --perf --reps N` runs only the plain loop with no clock calls inside it, which is what `perf stat`
should measure. `--impl none --perf` generates the workload and does nothing else (baseline for `perf`).

On Linux (needs `perf`): `./scripts/run_perf.sh` writes `results/perf_stat_*.txt` and `results/perf_report_*.txt`.
`g++` on macOS is Apple clang; on Linux it is GNU g++. The workload and checksum are identical on both
(only raw `std::mt19937` output is used, never `std::uniform_*_distribution`).

## 2. Interface changes (vs the PDF)

1. **Prices are integer ticks** (`int32_t`, 1 tick = 0.01, so 100.01 is 10001), not `double`. `to_ticks()` and
   `to_price()` convert at the edges. `Level.price` and `BBO.*_price` are ticks too.
2. **`add_order` returns `bool`** (`false` = rejected: duplicate live id, quantity <= 0, price outside its band).
   The spec's `void` version would silently accept bad input.
3. Everything else is as specified: `cancel_order` returns `false` for unknown / already-cancelled ids,
   `get_bbo()` on an empty book has `has_bid == has_ask == false`, `get_bids/get_asks(depth = 10)` return as many
   levels as exist (best first).
4. Implementation B additionally requires order ids below 2^24 (16.7 M); larger ids are rejected. See section 3.

## 3. Data-structure choices

The three designs differ in *every* one of the four things the spec lists.

| | A: standard library | B: contiguous + direct index | C: price-indexed (mine) |
|---|---|---|---|
| **Order storage** | `unordered_map<id, Rec>`: one heap node per order | `vector<Rec>` indexed **directly by id** (no hashing) | custom **open-addressing hash table**, one flat pre-allocated array |
| **Price levels** | `std::map<price, qty>` (red-black tree, one node per level) | one **sorted `vector<Level>`** per side, best level kept at the back | fixed **array with one slot per tick** (5000 per side); the tick *is* the index |
| **Finding best bid/ask** | `map.begin()` | `vector.back()` | cached `best_bid_/best_ask_` index; after the best level empties, a **bitmap scan** (clz/ctz, 64 levels per step) finds the next one |
| **Locating an order to cancel** | hash lookup, then tree search for its level | `orders_[id]` is O(1), then binary search for its level | one hash probe gives (tick, side, qty), then a direct array index |
| **Memory** | many small allocations | two contiguous vectors + id table that grows by doubling | 24 MB hash table + 80 KB of level arrays, allocated once |

### Big-O (L = live price levels on one side, at most 5000; W = 79 bitmap words)

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | O(log L) (tree) + O(1) avg (hash) | O(log L) search; O(L) memmove only when a **new** level is created | O(1) avg (probe + array write); amortised O(1) rehash |
| `cancel_order` | O(log L) + O(1) avg | O(log L) search; O(L) memmove only when the level **empties** | O(1) avg; O(W) bit-scan only when the **best** level empties |
| `get_bbo` | O(1) | O(1) | O(1) |
| `get_bids` / `get_asks` | O(depth) (pointer-chasing tree) | O(depth) (sequential vector) | O(depth + W) (bitmap walk) |

**Trade-offs I accepted on purpose.** B assumes small, dense ids (that is why it can skip hashing; real exchange ids
are sparse, so B would not survive production as written). C assumes a fixed tick size and a bounded price band
(that is why it can index prices directly). The spec gives me both assumptions, and C also has a general hash
for ids.

## 4. Correctness check

`./bench --verify` output (full text in `results/verify_output.txt`):

```text
Running correctness test...
  phase 1: 1000000 ops of the benchmark workload
  phase 2: 200000 ops of churn workload (levels empty and refill constantly)
Implementation A: PASS  (edge-case failures: 0, workload mismatches vs reference: 0)
Implementation B: PASS  (edge-case failures: 0, workload mismatches vs reference: 0)
Implementation C: PASS  (edge-case failures: 0, workload mismatches vs reference: 0)
Implementation C started with a 16-slot hash table (forces rehash + collision paths): PASS  (mismatches: 0)
  BBO + top-10 of both sides compared across A/B/C after every operation;
  complete book (every price level) compared with a naive reference periodically.

All implementations produce identical results.
```

What is checked:
- **Same random workload through all three.** After *every* operation the BBO and top-10 bids/asks must be
  identical across A, B and C, and the return value of every `add_order`/`cancel_order` must match.
- **A naive reference** (a map of live orders that rebuilds the levels from scratch on every query) is compared with
  all three, including the **complete book (all price levels)**, every 1000 ops (every 200 in the churn phase).
- **Hand-written edge cases**: empty book, cancel unknown / cancelled-twice, duplicate id, out-of-band prices and
  zero quantity rejected, band edges accepted, same-price quantities summed, best level removed -> next level
  becomes best, requesting more depth than exists, book emptied again.
- **Churn phase (48% add / 47% cancel):** the benchmark workload (70/20/10) only grows the book, so best levels
  almost never empty. The churn phase makes levels empty and refill constantly, which is what exercises "find the
  next best price".
- **A second copy of C starting with a 16-slot hash table**, so the rehash and backward-shift-deletion paths
  actually run (with the default 1M-slot table they never do).
- **I tested the tester.** I injected three bugs on a scratch copy and confirmed `--verify` reports `FAIL` for each:
  (1) C's backward-shift deletion disabled (caught only by the tiny-table copy), (2) C not recomputing the best ask
  when it empties (caught by the churn phase even with the hand-written cases switched off), (3) B not erasing
  empty levels. The checksum printed by the benchmark is also identical across implementations.

## 5. Benchmark method

- 1,000,000 operations, `std::mt19937 rng(42)`, generated **once** up front, so RNG cost is never timed and the
  three implementations replay byte-identical operations.
- Mix: 70% add, 20% cancel, 10% BBO. Every 100 operations `get_bids(10)` and `get_asks(10)` are also called
  (reported as "depth pair"). Side 50/50, quantity 1-100, price a uniformly random valid tick in the side's band,
  cancels always pick a currently-live order.
- `std::chrono::steady_clock`. **Total time**: a pass with no timers inside the loop. **Per-operation ns**: a second
  pass on a fresh book with each call bracketed by two clock reads; the cost of the clock read pair
  (calibrated, about 13 ns) is subtracted.
- **`get_bbo` is about 1 ns, below the clock's resolution** (on this machine `steady_clock` appears to advance in
  ~42 ns steps, which fits a 24 MHz counter; reading it costs about 13 ns), so per-call timing gave nonsense
  (negative!) numbers. It is timed as 32 back-to-back calls and divided. That is a
  warm-cache number; an empty `asm volatile` with a memory clobber stops the compiler merging the identical calls.
- 5 runs per implementation, median reported. The final BBO and a checksum are printed so the work cannot be
  optimised away: `Bid: 99.99 x 2971`, `Ask: 100.01 x 2974`, `Checksum: 4804449239647702760` (identical for A, B, C).
- **Machine for the timings:** Apple M3 Pro (5 performance + 6 efficiency cores, 128-byte cache lines, 16 MB L2 per
  performance cluster), 18 GB, macOS 27.0.1, Apple clang 21 (`g++`), `-O2 -std=c++17`. Single-threaded, no core pinning
  (macOS has no pinning), other apps were open.
- **Noise:** two full sessions gave totals of A 98.7 / 88.2 ms, B 42.2 / 40.5 ms, C 18.1 / 15.7 ms. Per-op numbers
  moved up to about 10 ns between sessions (C's `add`: 31 vs 20 ns). The ranking never changed. Treat the numbers
  as accurate to roughly +/-20%.

### Timing results (macOS, median of 5 runs, spec workload)

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 88.17 | 40.48 | **15.73** |
| ns / add | 73.32 | 27.48 | **19.90** |
| ns / cancel | 116.68 | 44.15 | **16.08** |
| ns / BBO | 1.26 | 1.23 | 1.28 |
| ns / depth pair (`get_bids(10)` + `get_asks(10)`) | 86.13 | **22.34** | 44.81 |

Extra workload (`--workload clustered`: prices cluster near the touch, like a real book; not required by the spec):

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 55.52 | 18.51 | 15.61 |
| ns / add | 37.00 | **4.73** | 18.48 |
| ns / cancel | 82.10 | 14.92 | 13.90 |
| ns / BBO | 1.34 | 1.23 | 1.15 |
| ns / depth pair | 66.69 | 19.55 | 39.56 |

## 6. perf results

**Environment:** Linux 6.12.54 (Docker Desktop VM) on aarch64, Ubuntu 24.04 container, `perf` 6.8.12, 11 vCPUs,
`perf_event_paranoid=2`. macOS has no `perf`, and no lab machine was available to me. Command, as in the spec
(`--perf --reps 10` makes the order-book loop run 10x per process so it dominates the identical workload
generation done at start-up; `-r 5` repeats the whole process 5 times):

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
./bench --impl A --perf --reps 10        # repeated for B and C
```

**Unavailable counters:** `cycles`, `instructions` (so IPC), `branches`, `branch-misses`, `cache-misses`,
`L1-dcache-load-misses` and `dTLB-load-misses` all print `<not supported>`: the VM does not expose the CPU's
performance-monitoring unit. Only the software counters `task-clock` and `page-faults` work.

### `perf stat`: Implementation A
```text
 Performance counter stats for './bench --impl A --perf --reps 10' (5 runs):
           1166.00 msec task-clock                       #    0.999 CPUs utilized               ( +-  1.83% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              7409      page-faults                      #    6.354 K/sec                       ( +-  1.79% )
            1.1667 +- 0.0214 seconds time elapsed  ( +-  1.83% )
```

### `perf stat`: Implementation B
```text
 Performance counter stats for './bench --impl B --perf --reps 10' (5 runs):
            655.21 msec task-clock                       #    1.000 CPUs utilized               ( +-  1.40% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             21432      page-faults                      #   32.710 K/sec                       ( +- 16.63% )
           0.65546 +- 0.00919 seconds time elapsed  ( +-  1.40% )
```

### `perf stat`: Implementation C
```text
 Performance counter stats for './bench --impl C --perf --reps 10' (5 runs):
            232.69 msec task-clock                       #    0.999 CPUs utilized               ( +-  4.41% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              2006      page-faults                      #    8.621 K/sec                       ( +-  7.69% )
            0.2330 +- 0.0103 seconds time elapsed  ( +-  4.41% )
```

### Baseline: workload generation only (`./bench --impl none --perf`)
```text
             14.10 msec task-clock    ...    970 page-faults ( +- 16.94% )
```
This cost (about 14 ms, 970 faults) is inside every number above and should be subtracted: it is the same for A, B and C.

### `perf record` / `perf report` (software `cpu-clock` sampling, `-g`, 10 reps, `results/perf_report_*.txt`)

| | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| `add_order` (tree/hash code is inlined into it) | 52.3% | 64.6% | 43.3% |
| `cancel_order` | 23.2% | 19.4% | 16.3% |
| `play<Book>` (driver loop + inlined `get_bbo`/depth queries) | 1.9% | 3.4% | 29.8% |
| allocator (`malloc`, `cfree`) + `_Hashtable::_M_rehash` | about 7.4% | n/a | n/a |
| `vector::_M_default_append` (B's id table growing) | n/a | 1.8% | n/a |
| `vector::_M_fill_assign` (C zero-filling its 24 MB table) | n/a | n/a | 2.8% |
| workload generation (`make_workload` + `mt19937`), computed from the baseline run: 14.1 ms / `task-clock` | 1.2% | 2.2% | 6.1% |
| unresolved libc samples (shown as `(deleted)`) | n/a | about 6% (probably `memmove`; **not confirmed**) | n/a |

Because the compiler inlines the container code into `add_order`/`cancel_order`, the profile says *which operation*
the time goes to, not *which line* inside it. **The sample counts are small: A 1,106, B 680, C 215.** On a figure
near 50% that is about +/-1.5, +/-2 and +/-3.5 percentage points (1 standard deviation), so only the large
differences are meaningful, and C's profile is the least precise.

## 7. Results table (the spec's section 10)

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms), macOS native | 88.17 | 40.48 | **15.73** |
| Total time per run (ms), Linux VM `(task-clock - 14.1 baseline) / 10` | 115.2 | 64.1 | **21.9** |
| ns / add | 73.32 | 27.48 | **19.90** |
| ns / cancel | 116.68 | 44.15 | **16.08** |
| ns / BBO | 1.26 | 1.23 | 1.28 |
| IPC | not supported | not supported | not supported |
| Branch misses (%) | not supported | not supported | not supported |
| L1-dcache-load-misses | not supported | not supported | not supported |
| Cache misses | not supported | not supported | not supported |
| Page faults (Linux VM, 10 runs per process; generation-only baseline = 970) | 7,409 | 21,432 (+/-17%) | **2,006** |
| `task-clock` (Linux VM, 10 runs per process) | 1166 ms | 655 ms | **233 ms** |

The macOS and Linux numbers come from different environments (native vs a VM on the same chip), so compare
*within* a row, never across rows. The ordering C < B < A holds in both environments.

## 8. Performance analysis

**Reading guide.** *Measured* = I have a number for it. *Hypothesis* = a mechanism that fits the data but that I
could not confirm, because the hardware counters needed to confirm it were unavailable.

### 11.1 Which implementation is fastest?

| | add | cancel | get_bbo | depth(10) pair |
|---|---|---|---|---|
| fastest | **C** (19.9 ns) | **C** (16.1 ns) | tie (about 1.2 ns) | **B** (22.3 ns) |

Measured: C is 3.7x faster than A on `add` and 7.3x on `cancel`; B sits in between. Total time: C 15.7 ms, B 40.5 ms,
A 88.2 ms.

**Why C wins add/cancel (partly measured, partly hypothesis).** C does no allocation, no search and no pointer
chasing: the tick is the array index (`price - 5000`), and the order is found with one hash probe. A pays for an
allocation per order (`unordered_map` node) *and* a root-to-leaf descent of a 5000-node tree (about 12 dependent
pointer hops through separately allocated nodes). B avoids allocation and tree nodes but does a 12-step binary
search over 5000 levels, and its worst case includes an O(L) `memmove` when a level is created or removed.
`perf record` supports the "allocation is a real cost in A" half: `malloc`/`cfree`/`_M_rehash` are about 7% of A's samples
and do not exist in B or C. The rest of A's cost sits inside the inlined tree and hash code, where `perf record`
cannot separate it.

**Measured evidence that access pattern matters.** In the clustered workload only the *input prices* change, not the
code. B's `add` falls from 27.5 ns to 4.7 ns and A's from 73.3 to 37.0 ns, but C's barely moves (19.9 to 18.5 ns).
That fits the hypothesis that A and B pay for data-dependent searches over memory that is cold and
branch-unpredictable when prices are random, and that become hot and predictable when prices cluster; while C's cost
is dominated by the random id -> hash-slot access, which does not depend on prices.

**`get_bbo`: a tie.** All three take about 1.2 ns: a few loads of an already-known position (`map::begin()`, `vector::back()`,
a cached index). The differences are below the measurement noise. This is the answer to hint 2: no implementation
searches the book for the BBO.

**Depth queries: B wins, C is slower than I expected, A is slowest.** B copies 10 adjacent elements from the end of a
contiguous vector. A walks 10 tree nodes (`_Rb_tree_increment`) scattered over the heap. C does a bit-scan per
level and builds a vector. Note that all three allocate the returned `std::vector`; part of every depth number is
`malloc`, not the data structure. (My first version of C forgot `reserve()` and measured 208 ns; with one reserve it is 42 ns.
I mention it because it shows how easily an allocation hides in a "fast" design.)

### 11.2 Branch prediction

**Which has the most branch misses? Not measured** (`branch-misses` is unsupported here). My prediction is **B**,
because `std::lower_bound` over 5000 levels takes about 12 data-dependent branches per `add` *and* per `cancel`, and with
uniformly random prices each is close to a coin flip. A's tree descent has the same problem (left/right per node); C
has the fewest data-dependent branches on its hot path: the probe loop almost always exits at the first slot, and the
other branches (`idx > best_bid_`, level became empty?) are heavily biased one way.

The measured result that supports this is the clustered-workload drop for B (`add` 27.5 -> 4.7 ns): with prices near
the touch every search takes the same path, which a branch predictor learns. It does not prove it, because cache
locality improved at the same time and I cannot separate the two without counters. **To confirm:** run
`./scripts/run_perf.sh` on a machine with a PMU and compare `branch-misses/branches` across A, B and C.

### 11.3 Cache behaviour

**Which has the most cache misses? Not measured; my prediction is A**, then B, then C, by memory layout:

- **A follows pointers and allocates many small objects.** Every order is its own `unordered_map` node and every level
  its own tree node, so a lookup is a chain of dependent loads to scattered addresses, and nothing is prefetchable.
- **B is contiguous.** The two level vectors are 5000 x 16 B = 80 KB each (L2-resident), and `add_order` writes the id
  table sequentially (ids increase), which is the best case for the hardware prefetcher. `cancel_order` reads it at a
  random id (an 11 MB table), which is where B misses.
- **C keeps the price side tiny** (two 40 KB arrays + a 632-byte bitmap, comfortably in L1/L2) **but its order table is
  24 MB and accessed at a random hash slot**, larger than the 16 MB L2 of one performance cluster. That one access is
  probably C's main remaining cost, and it is the same in both workloads, which matches its flat 19.9 vs 18.5 ns.

C's `Slot` is 24 bytes; getting it to 16 would shrink the table to 16 MB (see 11.6).

### 11.4 Page faults

**Measured:** B causes the most (21,432, with high run-to-run variance of +/-17%), then A (7,409), then C (2,006); the
generation-only baseline is 970 of each.

What it tells us about memory behaviour (the mechanism is my interpretation of the numbers):
- **B** keeps its id table in a `std::vector` that doubles as ids grow (1 -> 2 -> 4 ... -> about 1M entries of 16 B). Each
  doubling asks the kernel for fresh memory, zero-fills it (touching every page) and copies, so the faults come
  from *growth*. The high variance fits large blocks being returned to and re-obtained from the kernel by the
  allocator depending on timing. A one-line `orders_.reserve(...)` would remove most of it.
- **A** gets many small blocks from the allocator's arena; they are reused across my 10 repetitions, so it faults
  far less than B even though it makes millions of allocations. A low fault count does *not* mean A is cheap: its cost
  shows up in `task-clock`, not here.
- **C** allocates its 24 MB table once, up front. It faults little because this VM has transparent huge pages set
  to `always` (checked: `/sys/kernel/mm/transparent_hugepage/enabled`), so the kernel backs the table with 2 MB pages.
  On a machine without huge pages I would expect about 6000 faults per construction, so this number is
  environment-specific. macOS uses 16 KB pages, so counts there would differ again.

### 11.5 Production choice

If I could deploy only one, I would choose **C**, for this problem (one instrument, fixed tick, bounded price band), with conditions.
Not simply because it has the lowest average:

| Criterion | A | B | C |
|---|---|---|---|
| Latency | slowest (116 ns cancel) | middle | **fastest** |
| Predictability | per-order `malloc`/`free` on the hot path means unpredictable tail latency | O(L) `memmove` and vector regrowth can spike | no allocation on the hot path; worst cases are bounded: bit-scan <= 79 words, rehash only if the table is under-sized |
| Memory | grows with orders, fragmenting | grows with the *largest id*, not the live orders | fixed up front (24 MB + 80 KB) |
| Cache | pointer chasing | contiguous, but random id access | tiny price side; one cold table |
| Complexity | **simplest** | simple | **hardest** (backward-shift deletion, bitmap scans, tick arithmetic) |
| Maintainability | **best** (the standard library does the work) | good | needs the verifier as a safety net |

- **B is out for production** as written: it needs small dense ids, and real exchange order ids are large and sparse.
- **A is the right choice if the requirements change**: an unbounded price range, or price-time priority with per-level
  queues, or a team that must be able to read the code in a code review. Its latency is acceptable; its predictability is not.
- **C's conditions:** size the hash table for the expected live orders so it never rehashes on the hot path; keep
  `./bench --verify` (including the tiny-table and churn phases) as a regression test, because C's bugs are
  subtle (my mutation tests show the plain random workload would not catch them); and **measure p99/p99.9**, which I
  did not do. The averages above are what I can support, a production decision needs the tail.
- **Memory caveat:** 24 MB per instrument is fine for one instrument, wasteful for thousands. The table would have to be sized per instrument
  or shared.

### 11.6 What I would try next

1. **Get real hardware counters** (lab Linux machine; `./scripts/run_perf.sh` is ready) to confirm or refute the
   branch-miss and cache-miss hypotheses in 11.2 and 11.3. This is the first thing I would do.
2. **C: shrink `Slot` from 24 to 16 bytes** (32-bit qty, 16-bit tick index, 1 bit for the side) so the 2^20-slot table
   is 16 MB and fits the L2; **prefetch** the hash slot of the next operation (the stream is known in the benchmark).
3. **C: hierarchical (two-level) bitmap** so finding the next best level is 2 `ctz` steps instead of up to 79 words.
4. **B: `reserve` the id table up front** to remove the page faults, and replace `lower_bound` with a **branchless**
   binary search to test the branch-prediction hypothesis directly.
5. **All: let `get_bids/get_asks` fill a caller-provided buffer** instead of returning a `std::vector`: the depth
   numbers (22-86 ns) are largely `malloc`.
6. **A: a pool/arena allocator** for map and hash nodes, to see how much of its gap is allocation and how much is pointer chasing.
7. Pin the thread and isolate a core, compare `-march=native`, and measure **tail latency**, not just means.

## 9. Reproducing the numbers

```bash
g++ -O2 -std=c++17 bench.cpp -o bench
./bench --verify
./bench --impl all                  # timing table (macOS or Linux)
./scripts/run_perf.sh               # Linux with perf: writes results/perf_stat_*.txt and results/perf_report_*.txt
```

`results/` contains the raw outputs quoted above. In `run_perf.sh`, `BUILD_DIR` lets `perf` read symbols from a local
disk directory (I needed `BUILD_DIR=/tmp/ob_build` inside Docker because `perf` could not read symbols from a mounted folder).
