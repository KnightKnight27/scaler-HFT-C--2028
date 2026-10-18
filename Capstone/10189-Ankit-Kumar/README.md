# Capstone: Low-Latency Order Book

Team of 2:

1. Ankit Kumar
   - email: ankit.24bcs10189@sst.scaler.com
   - roll_no:24bcs10189
2. Pratham Jain
   - email: pratham.24bcs10083@sst.scaler.com
   - roll_no:24bcs10083

A resting order book for one instrument (no matching): add, cancel, BBO and depth queries.
We wrote it three times on three different data structures, proved that all three give
identical results, then benchmarked and profiled them to explain the differences.

| | Design | One-line idea |
|--|--|--|
| **A** | `std::map` levels + `std::list` per level + `std::unordered_map` index | the textbook node-based book; every order and level is its own heap object |
| **B** | sorted `std::vector` of levels (best at the back) + `std::unordered_map` | levels are contiguous and binary searched; orders live only in the hash map |
| **C** | *our design*: price-indexed arrays + two-level bitmap + flat Robin Hood hash table | no search, no pointers, no per-order allocation; every operation is O(1) |

**Headline (median of 5 runs, 1M ops):** C runs the workload in **17.4 ms** against 127.8 ms for B and 181.0 ms for A.
`add_order` takes **25 ns** in C against 102 ns (B) and 222 ns (A), and `cancel_order` **98 ns** against 358 ns and 398 ns.

## Files

| File | What it is |
|--|--|
| `common.h` | `Side`, `Level`, `BBO`, price band constants (in ticks), validation, `to_ticks` / `format_price` |
| `map_book.h` | Implementation A |
| `vector_book.h` | Implementation B |
| `array_book.h` | Implementation C, using `level_bitmap.h` (best-price search) and `order_table.h` (order id hash table) |
| `workload.h` | the random workload (fixed seed 42), generated once before timing starts |
| `verify.h` | the correctness test (`./bench --verify`) and a brute-force reference book |
| `bench.cpp` | the single benchmark program: `--impl A/B/C`, `--verify`, `--baseline` |
| `run_all.sh` | builds, verifies, benchmarks and profiles everything; all output lands in `results/` |
| `results/` | our raw outputs: `verify.txt`, `bench_X.txt`, `perf_stat_X.txt`, `perf_report_X.txt`, `cachegrind_X.txt`, experiments |

## How to build

```bash
g++ -std=c++17 -O2 -o bench bench.cpp
```

The books are header-only, so that one command builds everything. `run_all.sh` adds `-g`
so that `perf report` can show source lines; `-g` does not change the generated code.

## How to run the correctness test

```bash
./bench --verify
```

## How to run each benchmark

```bash
./bench --impl A        # 5 runs, prints each run and the median
./bench --impl B
./bench --impl C
```

Options: `--runs N`, `--ops N`, `--seed S`, `--ids sequential|scrambled|strided`, and `--no-latency` (skip the per-call timing pass).

perf (as in the assignment). `--runs 1 --no-latency` makes `perf stat -r 5` exactly 5 runs and keeps 2M clock reads out of the counters:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A --runs 1 --no-latency
perf record -g ./bench --impl A --runs 5 --no-latency
perf report
```

Or run everything at once. It writes all of `results/`:

```bash
./run_all.sh            # or: PERF=/path/to/perf ./run_all.sh
```

## Interface changes

1. **Prices are integer ticks, not `double`.** `99.98` is `9998` (`Price = int32_t`). Binary floating point can't hold 99.98 exactly, so comparing or hashing double prices is fragile. Ticks are exact and can index an array directly, which book C depends on. `to_ticks(double)` and `format_price(ticks)` convert at the edges only.
2. **`add_order` returns `bool` instead of `void`.** It returns `false`, and leaves the book unchanged, for a price outside the side's band (bid outside `[50.00, 99.99]`, ask outside `[100.01, 150.00]`), a qty outside `1..1,000,000,000`, or an id that is already live. All three books apply exactly the same rules, and `--verify` tests them.
3. **`Qty = int64_t`** (the same size as `long` on Linux x86-64). `Level` and `BBO` use `Price` and `Qty`, and they gained `operator==` for the correctness test.
4. **Constructors take a capacity hint** (`expected_orders`). A and B pass it to `unordered_map::reserve` and C uses it to size its table. The benchmark gives all three the same hint (2^20), so nothing rehashes mid-run.
5. `cancel_order`, `get_bbo`, `get_bids(depth = 10)` and `get_asks(depth = 10)` keep the original signatures. The three queries are `const` in every book. `depth <= 0` returns an empty vector, and a deeper request than the book has returns what exists.

## Data-structure choices

L = price levels on one side (at most 5000), N = live orders, d = requested depth.

There is no matching, so no answer ever depends on the order of orders inside a level. Every book
needs two things: **price → total qty** (for BBO and depth) and **order id → (side, price, qty)** (for cancel).
The three designs solve these two problems in genuinely different ways:

| | A | B | C |
|--|--|--|--|
| **Price levels** | red-black tree nodes (`std::map`), one heap node per level | one contiguous sorted `std::vector<{price, qty}>` per side | fixed `std::array<Qty, 5000>` per side, indexed by price |
| **Orders** | `std::list` node per order inside its level (FIFO, time priority kept) | only in the hash map, as `{side, price, qty}` | only in a 16-byte slot of a flat hash table |
| **Best price** | `map.begin()` (the tree caches its leftmost node) | `vector.back()` (sorted worst → best) | cached index; when that level empties, `LevelBitmap::find_prev` |
| **Cancel lookup** | `std::unordered_map<id, Locator>`; the locator holds iterators to the level and the list node | `std::unordered_map<id, OrderRec>`, then `std::lower_bound` on the price | `OrderTable`: Robin Hood linear probing over one array |

### A: `std::map` + `std::list` + `std::unordered_map`

- **Data structures:** `std::map<Price, PriceLevel, std::greater<>>` for bids, `std::map<Price, PriceLevel>` for asks. `PriceLevel = {total qty, std::list<Order>}`. `std::unordered_map<OrderId, Locator>`.
- **Orders:** each order is a `std::list` node in its level, oldest first, which is the queue a matching engine would need.
- **Price levels:** tree nodes, sorted best-first by the comparator. A level is erased when its last order leaves.
- **Best bid/ask:** `begin()` of each map, O(1).
- **Cancel:** the hash map gives a `Locator` holding the map iterator (the level) and the list iterator (the order). Cancel unlinks the node, subtracts its qty, and erases the level if it is empty. Nothing is searched.

### B: sorted `std::vector` + `std::unordered_map`

- **Data structures:** one `std::vector<LevelEntry{price, qty}>` per side, sorted from the worst price to the best, and `std::unordered_map<OrderId, OrderRec{side, price, qty}>`.
- **Orders:** only in the hash map. Levels hold totals, not orders.
- **Price levels:** contiguous, 16 bytes each, at most 80 KB per side (fits in L2). Both vectors are `reserve`d to 5000 up front, so they never reallocate.
- **Best bid/ask:** `back()`. Because the best is at the back, a new best level is a `push_back` and removing the best level is a `pop_back`. Only levels created or emptied deep in the book shift memory.
- **Cancel:** hash lookup gives the price; `std::lower_bound` (about 13 steps for 5000 levels) finds the level; subtract; `erase` the level if it is empty.

### C (our design): price-indexed arrays + bitmap + Robin Hood hash table

- **Price levels:** `std::array<Qty, 5000>` per side: one 8-byte total per possible price, 40 KB per side. Both sides are stored so that **a higher index is a better price** (bids: `price - 50.00`, asks: `150.00 - price`), so one piece of code handles both.
- **Best bid/ask:** a cached index per side. An add can only improve it (one compare). When a cancel empties the best level, `LevelBitmap::find_prev` finds the next non-empty level: one bit per level in 79 64-bit words, plus a 2-word summary with one bit per non-empty word. It checks the current word and, if that is empty, jumps through the summary with count-leading-zeros. That is O(1) for 5000 levels, and the same walk serves `get_bids`/`get_asks`.
- **Orders:** `OrderTable`, one `std::vector` of 16-byte slots `{id, qty (u32), level (u16), side}`, 4 per cache line, with no per-order allocation. `qty == 0` marks an empty slot, so every 64-bit id is usable. The table is sized from the hint and grows ×2 above 75% load.
- **Cancel:** one probe usually finds the slot, which already holds the side, level index and qty. Then `qty[level] -= q` and, if the level emptied, a bitmap clear.
- **Hash function:** XOR-fold, `id ^ id>>21 ^ id>>42`. Sequential ids go to sequential slots, so adds walk the table in order. Collisions use **Robin Hood** linear probing with backward-shift deletion: no tombstones, and erase stops at the first entry already in its home slot. Both choices came from measurements; see [How C evolved](#how-c-evolved-hash-function-and-probing).

### Complexity

| Operation | A | B | C |
|--|--|--|--|
| `add_order` | O(log L) map find/insert + O(1) avg hash | O(log L) search; O(L) worst-case shift when a *new* level is inserted mid-book; O(1) avg hash | O(1) avg (hash insert + array index + bit set) |
| `cancel_order` | O(1) avg (hash + list unlink + iterator erase) | O(1) avg hash + O(log L) search; O(L) worst-case shift when a level empties mid-book | O(1) avg (hash + array; `find_prev` is ≤ 3 word scans) |
| `get_bbo` | O(1) | O(1) | O(1) |
| `get_bids(d)` / `get_asks(d)` | O(d) | O(d) | O(d), one `find_prev` per level |

Memory per live order: A ≈ 112 B (64 B hash node + 48 B list node, plus buckets), B ≈ 48 B (hash node, plus buckets),
C = 16 B per slot (a fixed 16 MB table at the benchmark's hint, whatever the order count).

## Correctness results

`./bench --verify` checks each implementation against `ReferenceBook`. That is a deliberately naive book: one qty per tick in a
plain array, with every query scanning the price band, so it is easy to see that it is right. The checks:

- **Edge cases:** empty book, cancel of an unknown id, double cancel, insufficient depth (3 levels, ask for 10), `depth <= 0`, both edges of both price bands, bids/asks at 100.00, qty ≤ 0, a duplicate live id, ids `0` and `2^64-1`, and walking the best bid down through all 5000 levels.
- **Random workloads,** compared after **every** operation (return value, `get_bbo`, `get_bids(10)`, `get_asks(10)`, plus a second cancel that must fail), and on full depth every 1000 ops:
  the benchmark workload itself; a "churn" workload on the 8 ticks nearest the mid, where levels empty and refill and the best moves constantly; a "sparse" book with big gaps; and two id patterns that cause hash collisions (scrambled 64-bit ids, and ids `k << 20`). The books start with room for 64 orders, so every hash table rehashes many times.

To check that the test can actually fail, we broke book C on purpose, one bug at a time: a best-price fallback that skips a level, a broken backward-shift erase, and a wrong Robin Hood swap rule. `--verify` reported `FAIL` for C every time.

```
Implementation A: PASS  (8896670 checks, 0 failed)
Implementation B: PASS  (8896670 checks, 0 failed)
Implementation C: PASS  (8896670 checks, 0 failed)

All implementations produce identical results.
```

The benchmark checksum (a fold of every query result) is also identical for all three: `13891949950710415644`, with final BBO `99.99 x 2722 / 100.01 x 2515`.

## Benchmark method

- **Workload** (`workload.h`): 1,000,000 ops with `std::mt19937(42)`: 700,471 adds, 199,305 cancels and 100,224 `get_bbo`. `get_bids(10)` and `get_asks(10)` are called after every 100 ops. Side is 50/50, qty is 1–100, and the price is a uniform tick in the side's band. A cancel always picks a random *live* order. The stream is generated **before** timing, and we map random numbers to ranges by hand rather than with `std::uniform_int_distribution`, so the stream is identical with any standard library.
- **Each run** builds a fresh book and replays the stream twice:
  1. **Throughput pass:** no timers inside the loop. Gives **total time**.
  2. **Latency pass:** each call wrapped in `steady_clock::now()`. Gives ns per op type (avg, p50, p99, p99.9, max). The ~15 ns cost of a clock pair is included; the benchmark prints it.

  Also, because one `get_bbo` costs about the same as a clock read, we time 10M back-to-back `get_bbo` calls with a single clock pair.
- **5 runs, median reported.** An `asm volatile` barrier keeps every result alive, and the final BBO and checksum are printed.
- **Environment:** i5-13450HX laptop on AC power, Ubuntu 24.04 in **WSL2**, g++ 13.3 `-O2`, process pinned with `taskset -c 2` (`results/system.txt`).

**Hardware counters are not available on this machine.** The WSL2 kernel log says
`Performance Events: unsupported CPU family 6 model 183 no PMU driver, software events only.`
Hyper-V does not expose the PMU of this hybrid Raptor Lake CPU to the VM, so `cycles`, `instructions`,
`branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and `dTLB-load-misses` all show `<not supported>`. IPC cannot be measured.
`task-clock` and `page-faults` work. To still get instruction, cache and branch numbers, we ran every book under **valgrind cachegrind**
(`--cache-sim=yes --branch-sim=yes`). Those counts are *simulated*: exact instruction counts, a modelled cache hierarchy and a simple branch predictor.
`perf record` falls back to the `task-clock` software event, which is fine for finding hot spots.
On a Linux machine with a PMU, `run_all.sh` fills in all the hardware counters unchanged.

## Results

### Results table

Benchmark numbers: `results/bench_X.txt` (median of 5). perf: `results/perf_stat_X.txt` (`perf stat -r 5`). Simulated counters: `results/cachegrind_X.txt`.

| Metric | Implementation A | Implementation B | Implementation C |
|--|--|--|--|
| Total time (ms), throughput pass | 181.0 | 127.8 | **17.4** |
| ns / add (avg · p99) | 222.1 · 552 | 101.8 · 237 | **24.9 · 40** |
| ns / cancel (avg · p99) | 398.0 · 792 | 358.0 · 740 | **98.0 · 296** |
| ns / BBO (timed per call, incl. ~15 ns clock) | 27.5 | 24.9 | 23.1 |
| ns / BBO (10M back-to-back calls) | 0.75 | 0.95 | 0.92 |
| ns / get_bids(10)+get_asks(10) | 175.2 | 144.4 | 137.8 |
| IPC | n/a (no PMU) | n/a | n/a |
| Branch misses (%), hardware | n/a | n/a | n/a |
| Branch mispredicts, cachegrind (rate) | 8.24 M (6.3%) | 9.12 M (5.0%) | **1.68 M** (7.1%) |
| L1-dcache-load-misses, hardware | n/a | n/a | n/a |
| D1 read misses, cachegrind | 10.21 M | 9.27 M | **1.65 M** |
| Cache misses, hardware | n/a | n/a | n/a |
| Last-level data misses, cachegrind | 4.12 M | 2.00 M | **1.17 M** |
| Instructions, cachegrind | 851.6 M | 825.7 M | **309.7 M** |
| Page faults (perf) | 22,957 | 14,971 | **11,111** |
| task-clock (perf, whole process) | 339.5 ms | 190.3 ms | **48.9 ms** |
| Memory (RSS growth while building) | 61.7 MB | 30.6 MB | **16.1 MB** |

Every process also pays a fixed cost: building the workload, plus startup. `./bench --baseline` measures it as 30.2 ms task-clock, 6,971 page faults, and in cachegrind
193.6 M instructions, 1.02 M D1 misses, 0.82 M LL misses and 0.86 M mispredicts. **With that subtracted, the books themselves:**

| book only | A | B | C |
|--|--|--|--|
| instructions | 658 M | 632 M | **116 M** |
| D1 misses | 11.93 M | 9.41 M | **1.33 M** |
| LL data misses | 3.30 M | 1.17 M | **0.34 M** |
| branches / mispredicts | 119 M / 7.37 M | 169 M / 8.26 M | **11.3 M / 0.82 M** |
| page faults | 15,986 (≈ 65 MB) | 8,000 (≈ 33 MB) | **4,140 (≈ 17 MB)** |

### perf stat output

```
 Performance counter stats for './bench --impl A --runs 1 --no-latency' (5 runs):

            339.50 msec task-clock                       #    0.977 CPUs utilized               ( +-  3.85% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             22957      page-faults                      #   67.620 K/sec                       ( +-  0.00% )

            0.3474 +- 0.0125 seconds time elapsed  ( +-  3.60% )
```
```
 Performance counter stats for './bench --impl B --runs 1 --no-latency' (5 runs):

            190.33 msec task-clock                       #    0.957 CPUs utilized               ( +-  0.57% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             14971      page-faults                      #   78.659 K/sec                       ( +-  0.00% )

           0.19882 +- 0.00171 seconds time elapsed  ( +-  0.86% )
```
```
 Performance counter stats for './bench --impl C --runs 1 --no-latency' (5 runs):

             48.92 msec task-clock                       #    0.908 CPUs utilized               ( +-  0.76% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             11111      page-faults                      #  227.127 K/sec                       ( +-  0.00% )

          0.053862 +- 0.000363 seconds time elapsed  ( +-  0.67% )
```

### perf record: where the time goes

Full reports are in `results/perf_report_X.txt` (by function) and `results/perf_report_srcline_X.txt` (by source line, which matters because most hot code is inlined STL).

- **A:** `add_order` 28.1%, `cancel_order` 16.1%. Then glibc malloc internals: `unlink_chunk` 11.1%, `malloc_consolidate` 11.1%, `_int_free`/`malloc`/`_int_malloc` ≈ 5%. Destroying the book takes another 19.3% (`_Rb_tree::_M_erase` 7.7% + 7.5%, `~_Hashtable` 4.1%).
  By source line, the top two are `stl_tree.h:790` (`_S_right`: following a tree pointer) at **19.1%** and `new_allocator.h:172` (`operator delete`) at **18.9%**.
- **B:** `cancel_order` 27.1%, `std::__lower_bound` 24.4% (asks 12.8% + bids 11.6%), `add_order` 7.3%, hash-table `erase` 6.7%, malloc internals ≈ 14%.
  By line: `hashtable_policy.h:528`, which is `__num % __den`, at **15.8%**. libstdc++ uses a prime bucket count, so every lookup does a 64-bit division. Next is `stl_function.h:378`, the key compare that first touches the hash node, at 10.7%, and the `lower_bound` loop at ≈ 15%.
- **C:** `cancel_order` 29.8%, `add_order` 17.8%, the inlined replay loop in `main` 13.9%. Generating the workload (`make_workload` + `mt19937`, 16%) is now a visible share, because the book itself is so cheap. Kernel page-fault handlers and the table's zero fill (`_M_fill_assign` 3.5%) together take about 9%.
  By line: `order_table.h:86` at **24.9%**, the first load of the probed slot in `find()`. That is the one cache miss a cancel cannot avoid: it goes to a random old order.

## Performance analysis

### 11.1 Which implementation is fastest?

**C, for every operation, and 7× (vs B) to 10× (vs A) on total time.** A cache miss costs more than an instruction, so the explanation is mostly memory access:

- **`add_order`: C 25 ns, B 102 ns, A 222 ns.** C's add does an array index into 40 KB (`qty[price]`, L1/L2 resident) plus one write to the order table. With sequential ids and the fold hash, that write lands in the slot right after the previous order, usually in the same cache line. Nothing is allocated. B allocates a hash node (`malloc`) and computes a 64-bit `%` for the bucket. It then binary-searches 5000 levels, about 13 dependent loads with unpredictable branches. A does all of B's hash work, allocates a second node for the list, and walks about 13 levels of red-black tree whose nodes sit wherever `malloc` put them: one dependent load per level.
- **`cancel_order`: C 98 ns, B 358 ns, A 398 ns.** A cancel targets a random live order, so every design pays at least one miss to wherever that order lives. C pays exactly that one: the probed slot already holds the level index and qty, and the level arrays are hot. B pays a miss on the bucket array (8 MB) and then a dependent miss on the node, plus a `%`, a `free` and a binary search. A pays the bucket and node misses, then unlinks a list node, which writes to its two neighbours (two more random lines), then frees two nodes.
- **`get_bbo`: a tie.** All three are O(1) and cost about 1 ns in a tight loop (A 0.75, C 0.92, B 0.95 ns). The 23–28 ns per-call figures are almost entirely the 15 ns clock pair. A keeps the leftmost node cached, B reads `back()`, C reads a cached index. Each is one or two loads that hit L1.

The two passes also differ. C's throughput pass costs 17.4 ns/op, while its timed calls average about 39 ns per op (about 24 ns after removing the 15 ns clock pair). A shows the same pattern: 181 ns/op against about 225 ns.
The clock read is ordered (the vDSO uses an ordered `rdtsc`), so each timed call runs on its own. Untimed, the out-of-order core overlaps the cache misses of consecutive independent operations. Per-call latency and throughput are different questions, which is why we report both.

### 11.2 Branch prediction

Hardware branch counters are not available here, so these are cachegrind's simulated numbers. **B has the most mispredicts:** 8.26 M from the book itself, against 7.37 M for A and 0.82 M for C.
In B, the cause is `std::lower_bound`: each of the ~13 steps branches on `level.price < price`. For a uniformly random target price that branch is a coin flip, so no predictor can learn it, and about half the steps mispredict. A has the same problem in its tree descent (`key < node.key` at every level), plus branches inside `malloc`/`free` and the hash chain walk. Its *rate* (6.3%) looks higher only because it runs fewer branches overall.
C executes 15× fewer branches than B, and most of what remains is predictable: `slot empty?` and `level was empty?` almost always go the same way. A large part of C's 0.82 M is the benchmark's own `switch` on a random op type, which every book pays. Lesson: compare absolute mispredict counts, not the percentage. C has the "worst" rate (7.1%) and by far the fewest misses.

### 11.3 Cache behaviour

**A has the most cache misses:** 11.9 M D1 and 3.3 M last-level for the book alone, against 9.4 M / 1.2 M for B and 1.3 M / 0.34 M for C.

| | contiguous memory? | jumps around? | follows pointers? | many small allocations? |
|--|--|--|--|--|
| A | no | yes | yes: tree, list and hash chains | yes, 2 per add (list node + hash node), 2 frees per cancel |
| B | levels yes (80 KB/side) | binary search over L2-resident data; hash nodes are scattered | yes: bucket → node | yes, 1 per add |
| C | yes: level arrays, bitmap and slot table are all flat arrays | only the cancel's one random slot | no | none (one 16 MB array allocated up front) |

Pointer chasing is the expensive pattern: the CPU can't start the next load until the current one returns, so misses serialize.
A's tree descent is about 13 dependent pointer loads, and its order data is spread across hundreds of thousands of separately allocated nodes. B keeps the level search inside 80 KB, which is why its LL misses are about a third of A's, but its order lookup still chases bucket → node.
C touches about two cache lines per operation, and for adds those lines are sequential, which the hardware prefetcher handles well.

### 11.4 Page faults

**A causes the most page faults:** 15,986 above the baseline (≈ 65 MB of 4 KB pages, matching its 61.7 MB RSS growth). B causes 8,000 (≈ 33 MB) and C 4,140 (≈ 17 MB).
That is the memory footprint: A uses about 112 B per order across two heap nodes plus buckets, B about 48 B plus buckets, and C a fixed 16 B per slot.
*When* the faults happen matters as much as how many. C's 4,096 table pages are all touched in the constructor (the `assign` zero-fill), before the first order arrives.
A and B fault in the hot path whenever `malloc` grows the heap during adds. Some of their `max` latencies, hundreds of µs, are page faults and kernel work.
A also pays heavily at teardown. Freeing about 1 M nodes (`_M_erase`, `~_Hashtable`) plus the allocator's free-list work (`malloc_consolidate`, `unlink_chunk`) is about 40% of A's `perf record` samples. That work is outside the timed loop, but a real process pays it.

### 11.5 Production choice

**We would deploy C**, with eyes open about its costs:

- **Latency:** fastest at every operation and 7–10× the throughput.
- **Predictability:** the strongest reason. C's p99 (add 40 ns, cancel 296 ns) and p99.9 (177 / 461 ns) are tighter than A's and B's. Its hot path never calls `malloc`, never takes a page fault after construction, and never does more than O(1) work: no rebalancing, no memmove, no rehash at the configured capacity. A and B have `malloc` in the hot path, which has its own slow paths, and B has an O(L) shift when levels appear mid-book.
- **Memory:** smallest (16 MB against 31 / 62 MB) and fixed. In production we would size the table for the worst day and alert if it ever grows, rather than rehash 16 MB mid-session.
- **Cache behaviour:** flat arrays, 2 lines per op, no pointers.
- **Complexity / maintainability:** C's weak spot. A is about 130 lines of standard containers that any C++ developer can read. C needs its own Robin Hood table and bitmap, about 180 lines with subtle invariants (we got the erase wrong at first, see below). It also assumes a **fixed price band**: a new band, or an instrument that drifts far, needs re-centering. And it keeps no per-order FIFO, which a matching engine would need. We would mitigate this by keeping A, or `ReferenceBook`, as the test oracle; `--verify` is exactly that differential test, and it catches the bugs we injected.

A is the "correct by construction" choice for a prototype or for instruments with unbounded prices. For a latency-critical book on a known band, C's predictability is worth the extra code.

### 11.6 What we would try next

1. **Huge pages for the order table** (`madvise(MADV_HUGEPAGE)`): 16 MB is 8 × 2 MB pages instead of 4096 × 4 KB. The cancel's one random access currently also risks a TLB miss, which is a two-level page walk under WSL2's nested paging.
2. **Software prefetch in batches:** market data arrives in packets, so we could prefetch the slot of the next message's order while handling the current one. The throughput-vs-latency gap above shows the core gains a lot from overlapping misses.
3. **Direct-indexed orders** when ids are dense (exchange sequence numbers): a ring buffer indexed by `id - base` replaces hashing and probing entirely.
4. **For A:** a pool allocator (`std::pmr::unsynchronized_pool_resource`) to take `malloc`, page faults and the expensive teardown out of the picture. **For B:** a power-of-two hash table (removes the `%`, 16% of B's time) and a branchless or Eytzinger-layout binary search.
5. **No allocation in depth queries:** `get_bids`/`get_asks` return a `std::vector`, so every call allocates. Writing into a caller-provided fixed array would remove most of the ~130 ns.
6. **FIFO per level for matching:** intrusive index-linked lists stored in the slots (prev/next as 32-bit indices), so C keeps time priority without pointers.
7. **Re-measure on bare-metal Linux** with a PMU for real IPC, branch-miss and cache-miss counters. `run_all.sh` is ready for it.

## Extra experiments

### How C evolved: hash function and probing

Our first C used Fibonacci hashing (`id × 0x9E3779B97F4A7C15 >> shift`), the usual choice for spreading keys. Measuring it changed our minds twice. Data in `results/history_C.txt` and `results/experiment_C_fibonacci_hash.txt`:

| C variant | total | add avg | cancel avg | cancel p50 | cancel p99 | cancel p99.9 | instructions |
|--|--|--|--|--|--|--|--|
| v1: Fibonacci + linear probing | 30.4 ms | 126.3 | 118.3 | 115 | 275 | 387 | 305.6 M |
| v2: XOR-fold + linear probing | 20.6 ms | 23.1 | 83.5 | 48 | 390 | 1429 | 396.4 M |
| **v3: XOR-fold + Robin Hood (final)** | **15.1 ms** | **23.5** | 87.7 | 98 | **263** | **437** | **309.7 M** |
| Fibonacci + Robin Hood (`-DORDER_TABLE_FIBONACCI_HASH`) | 29.2 ms | 125.8 | 120.4 | 108 | 270 | 438 | 306.7 M |

(v2 and v3 come from one back-to-back run; ns per call.)

- **v1 → v2:** Fibonacci hashing sends consecutive ids to random slots, so *every add* missed cache (126 ns). `std::unordered_map` uses an identity hash, which is why B's adds were faster than C's at first. The XOR-fold keeps consecutive ids in consecutive slots: **add 126 → 23 ns**.
- **v2 → v3:** cachegrind then showed v2 running 90 M *more* instructions than v1, and the cancel tail got worse (p99.9 1.4 µs). The cause was plain linear-probing deletion. It has to scan to the end of the occupied run, and with sequential ids the run after a recent order stretches to the newest id, hundreds of slots. Robin Hood insertion keeps runs ordered by home slot, so the erase stops at the first entry already at home. Result: **p99.9 1429 → 437 ns**, 22% fewer instructions, and 20.6 → 15.1 ms total. The cancel median rose (~50 → ~97 ns, the same in both rounds of the back-to-back run). We haven't pinned down why without hardware counters, but for a trading system we'll trade a slower median for a 3× tighter tail.

### What if order ids are not sequential?

`./bench --impl X --ids scrambled` uses random-looking unique 64-bit ids (`results/experiment_X_scrambled_ids.txt`):

| | A | B | C |
|--|--|--|--|
| sequential ids: total / add / cancel | 181.0 ms / 222 / 398 | 127.8 ms / 102 / 358 | 17.4 ms / 25 / 98 |
| scrambled ids: total / add / cancel | 230.5 ms / 284 / 416 | 153.0 ms / 171 / 339 | 34.9 ms / 114 / 116 |

Every book slows down once ids lose their locality, because now every add touches a random part of the id index. C loses its sequential-add advantage (25 → 114 ns), but it is still the fastest at every operation and 4.4× B's throughput. Its cancel changes much less (98 → 116 ns), because a cancel was already a random access.

## Notes and limitations

- WSL2 has no PMU here, so there is no IPC and no hardware cache, branch or TLB counters (see above). Cachegrind's cache and branch models are simplified and give *relative* comparisons, not exact hardware numbers.
- `taskset` pins to a WSL vCPU, but Windows decides which physical core (P or E) runs that vCPU. Run-to-run spread is about ±10%; the median of 5 smooths most of it. An earlier full run on **battery** came out 2–3× slower across the board, so every number here is from a run on AC power.
