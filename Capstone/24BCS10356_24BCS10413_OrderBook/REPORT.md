# Performance Report — Low-Latency Order Book

**Team:** Shambhu Yadav (24bcs10356, shambhu.24bcs10356@sst.scaler.com) · Ratnesh Vaibhav (24bcs10413, ratnesh.24bcs10413@sst.scaler.com)

Design details for A, B and C are in `README.md`; this report covers results and analysis.

## 1. Setup

| | Graviton (`results/`) | Ryzen (`results/x86/`) |
|---|---|---|
| Machine | AWS EC2 `c7g.large`: 2 vCPU AWS Graviton3 (Arm Neoverse-V1), L1d 64 KiB/core, L2 1 MiB/core, L3 32 MiB | AMD Ryzen 5 5600H laptop (x86-64, Zen 3): 6 cores, L1d 32 KiB/core, L2 512 KiB/core, L3 16 MiB; `powersave` governor (amd-pstate, balance_performance), on AC power, benchmarks pinned with `taskset -c 3` |
| OS / kernel | Ubuntu 24.04, Linux 7.0.0-1014-aws | Ubuntu 24.04, Linux 7.0.0-38-generic |
| Compiler | g++ 13.3.0, `-std=c++17 -O2 -g -fno-omit-frame-pointer` | same |
| perf | 7.0.14, `kernel.perf_event_paranoid=1` | 7.0.14 installed, but `perf_event_paranoid=4` and no root access: no counters |
| Code | commit `c6352b0` (before the measurement fixes in §6) | commit `13f571c` (final code of this revision) |
| Timing | `./bench --impl X`: 5 runs, median (`results/bench_*.txt`) | `./scripts/bench_all.sh results/x86` (5 runs, median) |
| Counters | `perf stat -r 5 -e … ./bench --impl X --perf` (`results/perf_stat_*.txt`) | unavailable (see §3.2) |

Workload (seed 42): 1,000,000 ops (≈70 % add / 20 % cancel / 10 % `get_bbo`) plus 10,000
depth queries (`get_bids(10)` + `get_asks(10)` every 100 ops). Full machine details:
`results/machine.txt`. Earlier numbers from the development Mac are kept in `results/macos/`.

## 2. Correctness

`./bench --verify` feeds the same workload to A, B, C and a deliberately naive reference model
(`src/reference_book.hpp`: quantity per tick in a plain array, BBO and depth by scanning it) in
lock-step. After **every** operation it compares each book with the reference: the add/cancel
return value, the BBO, and every level of `get_bids(10)` / `get_asks(10)`. It also runs
hand-written edge cases (empty book, unknown and double cancel, 3-level book asked for 10, BBO
falling to the next level, zero/negative/huge depth, invalid price or quantity, id 2^64-1).
**A PASS, B PASS, C PASS: all implementations produce identical results** on both machines
(`results/x86/verify.txt`; on Graviton, `results/verify.txt` comes from the earlier harness that
compared B and C with A). Checking against a reference rather than
against A matters: with A deliberately broken (emptied levels never erased), the earlier
A-as-yardstick harness failed all three books; the current one fails A alone.

`tests/test_orderbook.cpp` adds what the benchmark workload never sends (all 10,000 prices
through the `double` API, a full 5,000-level side, invalid and wrapping prices, random 64-bit
ids, duplicate and repeated operations, and C starting from a 16-slot table so `grow()` runs):
120,156 checks, 0 failures (`results/x86/tests.txt`). When these tests were added they failed 22
checks against the original book code: the price and id bugs fixed in §6, items 1–2. Tests and `--verify` are also clean under
ASan + UBSan (`make sanitize`).

## 3. Results table

### 3.1 Graviton3, Arm (commit `c6352b0`, with perf counters)

| Metric | A (map + hash) | B (sorted vector + id tree) | C (arrays + bitmap + own hash) |
|---|---|---|---|
| Total time (ms) | 204.1 | 427.7 | **45.3** |
| ns / add | 131.8 | 264.0 | **93.0** |
| ns / cancel | 415.6 | 1156.0 | **82.8** |
| ns / BBO | 4.62 | **4.35** | 5.01 |
| ns / depth (10+10) | 137.8 | **66.5** | 88.5 |
| IPC | 1.07 | 1.07 | **1.64** |
| Branch misses (%) | 5.45 % (8.54 M) | 5.57 % (15.97 M) | **2.64 % (1.84 M)** |
| L1-dcache-load-misses | 10.92 M | 25.22 M | **3.31 M** |
| Cache misses | 10.93 M | 25.23 M | **3.27 M** |
| dTLB-load-misses | 17.55 M | 40.44 M | **2.34 M** |
| Page faults | **15,103** | 17,700 | 15,223 |

**How to read the table**
- *Total time* is the uninstrumented pass. It is the most reliable number.
- *ns / add* and *ns / cancel* time each call with a `steady_clock` pair. **Correction:** this
  table subtracted 63 ns, the wall time of a timing-loop iteration that contains *two* clock
  reads; a timed call only carries about one read's worth. These "net" values are therefore too
  low by roughly one clock read (on the Ryzen that read is 23.5 ns of a 45 ns iteration). The
  raw values in `results/bench_*.txt` are unaffected. They have not been re-measured on Graviton
  with the corrected benchmark.
- *ns / BBO* is 1 M back-to-back calls timed as one block. **Correction:** each result was folded
  through `fold_bbo()`, whose two `lround()` calls cost ~7.2 ns on the Ryzen against ~2.2 ns for a
  real `get_bbo()`, so this row mostly measures the checksum. The conclusion (all three tie)
  holds; see §3.2.
- *IPC* = instructions ÷ cycles. *Branch misses %* = branch-misses ÷ branches.
- **Counter availability.** All nine requested events were available. The core has fewer
  hardware counters than events, so perf multiplexed them (each was counted about 28–43 % of the
  time and scaled up). Run-to-run variation was under ±1.2 %.
- On Arm, perf's generic `cache-misses` event maps to the same hardware event as
  `L1-dcache-load-misses` (L1D refill), so those two rows match. Last-level-cache misses were
  not measured separately.
- perf counts the whole process, including generating the 1 M-op workload, which is identical
  for all three. `perf record` shows generation and RNG are about 30 % of C's samples and under
  10 % of A's and B's. The differences between columns therefore come from the books.
  `./bench --gen-only` now measures that shared cost alone for subtraction.

### 3.2 Ryzen 5 5600H, x86-64 (commit `13f571c`, corrected benchmark)

| Metric | A | B | C |
|---|---|---|---|
| Total time (ms) | 149.5 | 304.0 | **36.6** |
| ns / add (net of the 23.7–24.5 ns timer cost) | 114.5 | 201.5 | **98.5** |
| ns / cancel (net) | 312.6 | 743.7 | **78.7** |
| ns / BBO (burst, real `get_bbo`) | 2.26 | 2.20 | 2.21 |
| ns / depth 10+10 (net) | 139.5 | 127.0 | **123.4** |
| Page faults: workload generation | 6,900 | 6,900 | 6,899 |
| Page faults: book construction | 2,064 | 0 | 8,194 |
| Page faults: timed loop | 5,053 | 9,714 | **0** |
| IPC, branch misses, cache / L1 / dTLB misses | unavailable | unavailable | unavailable |

Hardware counters were **unavailable** on this machine: `perf stat` fails with
`perf_event_paranoid=4` and root access was not available to lower it. Page faults come from
`getrusage` around each phase instead of perf, which also separates construction from the loop.
The same total-time ranking as on Graviton appears: C 4.1x faster than A and 8.3x faster than B.

### 3.3 Tail latency (Ryzen, raw ns including the ~20 ns timer floor, median of 5 runs)

| | p50 | p90 | p99 | p99.9 |
|---|---|---|---|---|
| A add | 131 | 161 | 220 | 881 |
| B add | 170 | 230 | **1,773** | **6,373** |
| C add | 120 | 141 | 381 | **611** |
| A cancel | 321 | 491 | 771 | 1,904 |
| B cancel | 711 | 1,152 | 2,635 | 6,702 |
| C cancel | 110 | 141 | 401 | **591** |
| timer only | 20 | 30 | 31–60 | 31–70 |

`get_bbo` sits at the timer floor for all three (p50 20 ns, p99 31 ns for A and C; B's p99 is
90 ns). Maxima (4.7–261 µs) are not shown: the "timer only" row alone reaches 7–12 µs, so maxima
mostly record interrupts rather than the books. `steady_clock` advances in roughly 10 ns
steps here, which is why percentiles cluster on multiples of 10.

### 3.4 Measurement caveats
- **Latency vs throughput.** The timed per-call figures are latencies of isolated calls (the
  clock read stops the CPU overlapping one call with the next). The total is throughput. For A
  and B the two roughly agree: their mean latencies predict 143 and 288 ns per op against 148 and
  301 measured. For C they do not: the latencies predict 85 ns per op but the loop averages 36 ns.
  Our interpretation: C's operations are short (one probe, one array update), so several
  operations' cache misses on the 32 MB table are in flight at once. A and B each spend a long
  chain of dependent pointer loads inside one operation, which fills the out-of-order window.
- **Code layout.** Adding three *unused* members to `OrderBookC` moved its total time by ~1.3 ms
  (~4 %), and C before and after the id fix in §6 (which only adds a rarely taken path) measured
  ~4 % apart until both were compiled with `-falign-functions=64 -falign-loops=64
  -falign-jumps=64`, after which they matched. Build-to-build differences below
  ~5 % on C are not meaningful; the 4–8x gaps between designs are far outside that.
- **Laptop.** The Ryzen runs the `powersave` governor with boost on and could not be switched to
  `performance` without root. Around the 5-run median, the total varied by -4 to +4 % for C,
  -4 to +14 % for A and -5 to +18 % for B. The high end is each process's first run, which in
  both captures was the slowest for A and B, probably because it is the only run that touches
  fresh heap pages; later runs reuse memory the previous book freed (`results/x86/bench_*.txt`).
  The median is used for that reason.

## 4. Analysis

### 4.1 Which is fastest?
**C is fastest for add and cancel. All three tie on `get_bbo`. B is fastest for depth on
Graviton.** C runs the workload 4.5× faster than A and 9.4× faster than B on Graviton, and 4.1×
and 8.3× faster on the Ryzen.
- **C** adds with one hash probe into a flat table, one array add and one bit-set. It cancels
  with one probe and two stores. Neither touches the allocator.
- **A** allocates a hash node and descends about 13 red-black-tree nodes per add. perf record
  puts 60 % of its samples in `add_order`/`cancel_order`, plus libstdc++ hash-table internals.
- **B** walks the id tree (`std::map`, up to ~500k entries, ~19 levels deep) on every add and cancel, then
  binary-searches the level vector. A cancel also scans the level's order list linearly (13.5
  orders on average over the run: a level holds 26 orders on average when a cancel arrives and
  about 50 by the end), so cancel is B's worst operation (33 % of samples). perf record also shows 7.4 % in
  `vector::_M_insert_aux`: inserting a new level moves every later `PriceLevel` one slot.
  Each holds its own vector, so this is a move per element, not a single memmove. Another 5.9 %
  is `_Rb_tree_rebalance_for_erase` from the id tree.
- **`get_bbo`** is O(1) in all three (`map.begin()`, `vector.back()`, cached `best` rank). With
  the corrected burst it costs 2.20–2.26 ns on the Ryzen for all three. In a tight loop the few cache
  lines involved stay in L1, so all three designs cost the same.
- **Latency vs throughput:** on the Ryzen, C's add is only 1.2x faster than A's per isolated call
  (98.5 vs 114.5 ns) but 4.1x faster over the whole run. C's short operations let consecutive misses
  overlap; A's do not (§3.4).
- **Depth:** on Graviton B wins because its top 10 levels are the last 10 contiguous elements. C
  computes the next set bit for each level. A follows tree pointers. On the Ryzen the three are
  within 15 % of each other (C 123, B 127, A 140 ns net), so this ranking is machine-dependent.

### 4.2 Branch prediction
**B has the most branch misses (16.0 M), then A (8.5 M), then C (1.8 M).** A and B have a similar
miss *rate* (≈5.5 %), but B executes almost twice as many branches. Each tree descent compares
`key < node->key` with a random id or price, and the predictor can't learn a coin flip. A does
one tree descent per op. B does two (id tree and binary search), plus the scan loop in
`remove_order`, whose exit point is random and mispredicts once per cancel. Apart from picking the
buy or sell side (§4.6, item 5), C's data-dependent branches are the short probe loop (about 1.5
probes expected at load ≤ 0.5) and "did this level just become empty/non-empty", which is rare
once tens of orders rest per level. That is why C's rate is half the others' and its total is
4.6× below A's. All three counts also include generating the workload (§3.1), which is the same
for each.

### 4.3 Cache behaviour
**B has the most L1 misses (25.2 M), then A (10.9 M), then C (3.3 M). dTLB misses follow the same
order (40.4 M / 17.6 M / 2.3 M).**
- **A follows pointers and allocates many small objects.** Every order is a heap-allocated hash
  node, and every level is a heap-allocated tree node. A lookup jumps across about 13 scattered
  nodes plus a hash node. Each jump can be a miss, and the next load can't start until the
  previous one returns.
- **B is contiguous for levels and scattered for everything else.** The level vector and its
  binary search are cache-friendly. But the id tree has 500k scattered nodes, and every level's
  order list is a separate heap block. A cancel touches both, which is why B misses most and
  why its dTLB misses are highest: the accesses span many pages.
- **C accesses contiguous memory.** Its price arrays total 80 KB, so they stay in L1/L2. The
  order table is one 32 MB array, and a probe is usually one cache line. That line is random,
  because ids arrive in random order, so it is the one miss C cannot avoid. C's 3.3 M total also
  includes the misses from generating the workload.

These misses explain the IPC. A and B both sit at 1.07 instructions per cycle on a core that can
retire several per cycle, so they spend most cycles waiting for memory. C reaches 1.64 while
executing 1.8× fewer instructions than A.

### 4.4 Page faults
**B causes the most page faults (17,700). A (15,103) and C (15,223) are close.** Roughly 6,000
faults in every column (24 MB ÷ 4 KB pages) come from the pre-generated workload, which is
identical for all three.
- **B** keeps requesting fresh memory throughout the run: 10,000 per-level order vectors that
  grow by doubling (each growth allocates a new block and frees the old one) and 500k id-tree nodes.
- **A** faults steadily as it allocates about 700k hash nodes.
- **C** takes almost all its faults *at construction*, when it writes the empty marker into each
  of its 2²¹ slots (32 MB = 8,192 pages). After that the hot loop never allocates.
C's total is similar to A's, but its faults all happen at start-up instead of while trading.
The Ryzen run measures this directly with `getrusage` (§3.2): C takes 8,194 faults while
constructing the book and **0** during the timed loop; A takes 2,064 during construction (the
`reserve(1 << 20)` bucket array) and 5,053 during the loop; B takes 9,714, all during the loop.

### 4.5 Production choice
I would deploy **C**.
- **Latency:** lowest total time, and the lowest add and cancel latency.
- **Predictability:** no allocation in add, cancel or BBO after start-up (the order table grows
  only past 2^20 live orders; depth queries return a `std::vector` in every design), no
  rebalancing, no reallocation. Its slow paths are bounded or kept short: a scan of at most 79
  bitmap words when the best level empties, and a probe loop whose expected length the load
  factor keeps short. A and B call the allocator on every add and cancel and rebalance
  trees, which is where tail-latency spikes come from. The Ryzen percentiles bear this out
  (§3.3): C's p99.9 is ~600 ns for both add and cancel, against ~0.9–1.9 µs for A and ~6.4–6.7 µs
  for B. B's add p99 (1.77 µs vs a 170 ns median) points at its occasional expensive adds: a new
  level shifts every later `PriceLevel` (`_M_insert_aux`, 7.4 % of B's Graviton profile) and a
  growing per-level order vector reallocates; the Ryzen run did not separate the two. C's own p99 (~380–400 ns, 3x its median) is consistent with a cache
  and TLB miss on the 32 MB order table.
- **Memory:** a fixed 80 KB for prices plus 16 B per table slot. It is reserved up front, which
  costs 32 MB even for an empty book. That is a deliberate trade for no runtime allocation.
- **Cache behaviour:** 3.3–7.6× fewer L1 misses and 7.5–17× fewer dTLB misses than A and B.
- **Complexity and maintainability:** C is the most code, and its hash-table deletion needs a
  careful reviewer. The lock-step `--verify` test makes that acceptable. C also depends on the
  fixed tick size and 10,000-level band. If prices were unbounded, A would be the safer choice.
  B's layout of orders inside levels suits FIFO matching, which this book doesn't need.

### 4.6 What we would try next
1. Index orders by id in a plain array when ids are dense sequence numbers, removing hashing entirely.
2. Pack each order slot into 8 bytes to halve the table's cache and TLB footprint.
3. Use huge pages for the 32 MB table. C's dTLB misses (2.3 M) are close to its L1 misses (3.3 M).
4. Use a two-level bitmap so the worst-case "next level" search is 2 loads, not 79.
5. Index the two sides as `side_[int(side)]` to remove the buy/sell branch.
6. Give A and B pool allocators, to measure how much of their gap is allocator cost and how much
   is pointer chasing.
7. Time C's operations in batches rather than per call, since the clock costs more than the operation.

## 5. perf output

Graviton only (§3.1); hardware counters were unavailable on the Ryzen (§3.2).

### 5.1 `perf stat -r 5` — Implementation A
```
 Performance counter stats for './bench --impl A --perf' (5 runs):
            302.47 msec task-clock                                                              ( +-  0.48% )
         742851072      cycles                                                                  ( +-  0.47% )  (42.54%)
         795296019      instructions                                                            ( +-  0.48% )  (42.59%)
         156818194      branches                                                                ( +-  0.23% )  (42.92%)
           8543272      branch-misses                                                           ( +-  0.50% )  (28.74%)
          10929271      cache-misses                                                            ( +-  0.36% )  (28.73%)
          10923235      L1-dcache-load-misses                                                   ( +-  0.23% )  (28.67%)
          17554571      dTLB-load-misses                                                        ( +-  0.80% )  (28.35%)
             15103      page-faults                                                             ( +-  0.00% )
       0.305069301 +- 0.001471233 seconds time elapsed  ( +-  0.48% )
```
### 5.2 `perf stat -r 5` — Implementation B
```
 Performance counter stats for './bench --impl B --perf' (5 runs):
            573.06 msec task-clock                                                              ( +-  0.75% )
        1403229455      cycles                                                                  ( +-  0.72% )  (42.61%)
        1507807960      instructions                                                            ( +-  0.21% )  (42.79%)
         286597959      branches                                                                ( +-  0.12% )  (42.97%)
          15969673      branch-misses                                                           ( +-  0.22% )  (28.75%)
          25233190      cache-misses                                                            ( +-  0.50% )  (28.63%)
          25218384      L1-dcache-load-misses                                                   ( +-  0.71% )  (28.45%)
          40442071      dTLB-load-misses                                                        ( +-  0.73% )  (28.40%)
             17700      page-faults                                                             ( +-  0.00% )
       0.577182758 +- 0.004275806 seconds time elapsed  ( +-  0.74% )
```
### 5.3 `perf stat -r 5` — Implementation C
```
 Performance counter stats for './bench --impl C --perf' (5 runs):
            108.81 msec task-clock                                                              ( +-  0.28% )
         267452857      cycles                                                                  ( +-  0.26% )  (42.42%)
         438755509      instructions                                                            ( +-  0.28% )  (43.32%)
          69684402      branches                                                                ( +-  0.15% )  (44.23%)
           1841457      branch-misses                                                           ( +-  0.91% )  (28.96%)
           3265732      cache-misses                                                            ( +-  0.85% )  (28.02%)
           3309602      L1-dcache-load-misses                                                   ( +-  0.49% )  (27.72%)
           2336504      dTLB-load-misses                                                        ( +-  1.14% )  (27.75%)
             15223      page-faults                                                             ( +-  0.00% )
       0.109596579 +- 0.000313743 seconds time elapsed  ( +-  0.29% )
```
### 5.4 `perf record -g` — top functions (self time)
| A | B | C |
|---|---|---|
| 35.0 % `OrderBookA::add_order` | 33.3 % `OrderBookB::cancel_order` | 34.8 % `OrderBookC::add_order` |
| 25.3 % `OrderBookA::cancel_order` | 22.2 % `OrderBookB::add_order` | 15.6 % `make_workload` (not timed) |
| 6.3 % `make_workload` (not timed) | 7.4 % `vector<PriceLevel>::_M_insert_aux` | 7.5 % RNG distribution (not timed) |
| 4.3 % + 4.0 % `std::_Hashtable` internals | 5.9 % `_Rb_tree_rebalance_for_erase` | 7.3 % `OrderBookC::cancel_order` |
| 3.0 % `unordered_map::emplace` | 4.7 % `std::_Rb_tree` (id map) | ≈10 % kernel code (unresolved `[k]` symbols) |

Saved excerpts: `results/perf_report_{A,B,C}.txt`, the first 120 lines of each call-graph
report. They contain the top two symbols for A and B and the top five for C. The other
percentages in this table and in §3.1/§4 are not in these excerpts (the full reports were not
saved), so they cannot be re-checked from this repository. `scripts/perf_all.sh` now also saves a
flat profile (`perf_report_X_flat.txt`, every symbol ≥ 0.5 %) so a re-run keeps them.

## 6. Revision notes (branch `24bcs10413`)

Defects found by auditing the code against the spec, each with a regression test:

1. **Wrapping and off-tick prices.** `add_order(double)` used `static_cast<int>(lround(price*100))`;
   the 64-bit result was wrapped into an int, so a buy at 42949772.95 rested at 99.99 in all three
   books, and 100.013 was rounded to 100.01 despite the 0.01-tick rule. Now `price_to_tick()`
   rejects both, and NaN/inf.
2. **C rejected id 2^64-1** (its hash table's empty marker) while A and B accepted it. It now
   lives in a spare slot past the probed range.
3. **Benchmark timer cost** was subtracted twice over (loop-iteration wall time instead of one
   timed interval), and the **`get_bbo` burst** mostly timed `lround()` in the checksum fold.
4. **`--verify` used A as the yardstick**, so a bug in A would show up as B and C failing. It now
   checks every book against an independent reference model.

Added: tail-latency percentiles, per-phase page faults, `--gen-only`, strict CLI parsing,
`tests/test_orderbook.cpp`, `make test` / `make sanitize` / CTest, `scripts/bench_all.sh`, and
the Ryzen results in `results/x86/`. Not yet done: re-running the benchmark and `perf` on
Graviton with the corrected benchmark, and perf counters on the Ryzen.
