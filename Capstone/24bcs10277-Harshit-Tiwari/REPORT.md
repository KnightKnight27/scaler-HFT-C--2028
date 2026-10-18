# Performance Report: Three Order Book Designs

Team: Harshit Tiwari, 24BCS10277, harshit.24bcs10277@sst.scaler.com
and Tejas Varshney, 24BCS10313, tejas.24bcs10313@sst.scaler.com

- **A** `MapBook`: `std::map` price levels + `std::unordered_map` orders
- **B** `VectorBook`: sorted `std::vector` price levels + `std::map` orders
- **C** `ArrayBook`: flat array indexed by tick + cached best + our own open-addressing order table

Design details and Big-O are in the [README](README.md#the-three-designs).

## Setup

| | |
|---|---|
| CPU | Intel Core i3-1115G4 (2 cores / 4 threads, 48 KB L1d per core, 1.25 MB L2 per core, 6 MB L3) |
| OS | Windows 11, WSL2, kernel 6.18.54.1-microsoft-standard-WSL2 |
| Compiler | g++ 15.2.0, `-std=c++17 -O2` |
| perf | 7.0.14 (the WSL kernel `perf` 6.18 works for `stat` but is built without libelf, so `perf report` could not show symbols) |

All nine requested counters were available. WSL sets `perf_event_paranoid=2`, so
counters are user space only (`:u`).

**The laptop was busy during measurement.** Other jobs (Python processes) were
using about 2.5 of the 4 hardware threads, and perf showed the effective clock
anywhere between ~1.0 and ~2.7 GHz. To keep the comparison fair, `scripts/run_all.sh`
runs A, B, C interleaved for three rounds (each round = median of 5 runs), and
the table uses the median of the three rounds. Wall-clock numbers still vary by
10 to 20% between rounds. The counter-based conclusions are much more stable:
instruction and branch counts repeat to within ±0.1%.

## Results table

Per-op times include about 20 to 30 ns of `steady_clock::now()` overhead (see
the harness row below). p50 is the median single call. It is the more reliable
number here because the average gets pulled up by rare preemptions from the
other jobs.

| Metric | A | B | C |
|---|---:|---:|---:|
| Total time (ms) | 410.7 | 451.2 | **125.2** |
| ns / add (avg / p50 / p99) | 376 / 216 / 878 | 368 / 286 / 769 | **248 / 141 / 678** |
| ns / cancel (avg / p50 / p99) | 664 / 508 / 1467 | 1373 / 1076 / 2612 | **159 / 144 / 683** |
| ns / BBO (avg / p50 / p99) | 43.6 / 35 / 63 | 50.6 / 38 / 70 | 38.5 / 34 / 60 |
| ns / get_bids(10)+get_asks(10) | 544 | 444 | 363 |
| IPC | 0.58 | 0.60 | **0.84** |
| Branch misses (%) | 5.89% | 5.90% | **3.12%** |
| L1-dcache-load-misses | 42.1 M | 53.8 M | **14.7 M** |
| Cache misses | 12.1 M | 15.3 M | **6.9 M** |
| dTLB-load-misses | 2.04 M | 2.74 M | **1.38 M** |
| Page faults | **20,017** | 20,756 | 58,251 |

Counters come from `perf stat -r 5 ./bench --impl X --runs 1`
([A](results/perf_stat_A.txt), [B](results/perf_stat_B.txt), [C](results/perf_stat_C.txt)).
Timings come from [bench_A](results/bench_A.txt), [bench_B](results/bench_B.txt) and [bench_C](results/bench_C.txt).

### Removing the harness

`perf stat` counts the whole process: workload generation, two passes over the
book, clock reads and percentile sorting. To see the books alone, we ran the
same benchmark with a book whose methods do nothing
([results/experiments.txt](results/experiments.txt)). That harness costs
0.39 G cycles, 0.57 G instructions, 2.9 M branch misses, 0.8 M cache misses and
10,909 page faults. The do-nothing book also measures 34 ns p50 for `get_bbo`,
the same as all three real books, so **BBO time is below what this method can
resolve. It is effectively free in all three.**

After subtracting the harness (2,000,000 book operations per process):

| Book only | A | B | C |
|---|---:|---:|---:|
| Cycles per op | 1,180 | 1,537 | **390** |
| Instructions per op | 506 | 756 | **207** |
| IPC | 0.43 | 0.49 | 0.53 |
| Branch misses per op | 7.7 | 11.5 | **1.1** |
| Cache misses per op | 5.6 | 7.2 | **3.1** |
| L1 misses per op | 19.8 | 25.7 | **6.1** |
| Page faults | 9,108 | 9,847 | 47,342 |

### Where the time goes (perf record -g)

Full reports, flat profile first and then the call graph: `results/perf_report_*.txt`.

- **A**: `add_order` 38%, `cancel_order` 26%, `unordered_map` internals 8%,
  libc (malloc/free) about 10%. The call graph shows 20% of all samples are
  inside `std::map::operator[]` walking down the price tree.
- **B**: `cancel_order` 33%, of which 29% is the `std::map<id>` tree walk.
  `lower_bound` on the level vectors takes 15%, red-black rebalancing
  (`_Rb_tree_rebalance_for_erase`, `insert_and_rebalance`) about 11%,
  libc 10%.
- **C**: `add_order` 43%, almost all of it the inlined `find_slot` probe.
  `rebuild` takes 13% (table growth), the clock 12% and `cancel_order` 11%.
  No malloc shows up.

## 11.1 Which implementation is fastest?

**C is fastest for add and cancel. BBO is a tie.** C's total time is 3.3× lower
than A's and 3.6× lower than B's.

- **add_order**: C's add is one hash probe plus one array increment. A has to
  malloc a hash node and walk about 12 levels of the price `std::map`. B does the
  same kind of walk in a much bigger tree (about 19 levels of a 500k-node id
  tree), then a binary search over the level vector.
- **cancel_order** is where the designs separate (p50 144 vs 508 vs 1076 ns).
  A cancel hits a random live order, so its data is cold almost every time. C
  pays about one cache miss for the slot and then touches the hot 80 KB level
  array. A needs a bucket, then a node, then the price tree. B walks the id tree
  from the root, and its lower levels are spread over ~32 MB of nodes, so each
  step can miss in cache. Then it rebalances on erase.
- **get_bbo** is O(1) in all three (`begin()`, `back()`, cached ticks) and
  stays in L1 because it is read constantly. The measured 34 to 38 ns is the
  clock read.
- **Depth**: B and C read contiguous memory. A follows map pointers and is the
  slowest.

## 11.2 Branch prediction

**B has the most branch misses** (11.5 per op), then A (7.7), then C (1.1).

The misses come from searches whose direction depends on random data:

- In a red-black tree walk, each level is "go left or right". The key is random,
  so the predictor is close to a coin flip at every level. B does this over
  ~19 levels on both add (insert) and cancel (find), and A over ~12 levels on
  its price tree.
- B also runs `std::lower_bound` on the level vectors. GCC compiles it with a
  conditional branch per halving step (13 steps for 5,000 levels), and the
  price is random, so these mispredict too.
- A's `unordered_map` adds a short chain loop that is less predictable.
- C has almost no data-dependent branches. The probe loop normally stops at the
  first slot, the "is this a new best price" checks almost always go the same
  way, and the empty-level scan almost never runs because the book is dense.

## 11.3 Cache behaviour

**B has the most cache misses** (14.4 M book-only, 51 M L1 misses), then A,
then C.

| | Contiguous memory? | Jumps around? | Follows pointers? | Many small allocations? |
|---|---|---|---|---|
| A | no | yes | yes, map and hash nodes | yes, one node per order and per level |
| B | levels yes, orders no | yes, binary search and tree | yes, the id tree | yes, one tree node per order |
| C | yes, two flat arrays | one random slot per order op | no | no |

Pointer chasing hurts because each load depends on the previous one, so the CPU
cannot start the next miss early. A cancel in B is a chain of dependent loads
down the tree, and many of them go out to L3 or DRAM. That explains the low IPC
(0.49) and the 1,537 cycles per op.

C accesses memory randomly too, on purpose: hashing scatters ids over a 48 MB
table, which is far bigger than L3. But it is one random access per operation,
not a chain, and everything else it touches (the level array, the cached best
prices) stays in L1/L2. That is also why C has the lowest dTLB miss count even
with a large table.

## 11.4 Page faults

**C causes the most page faults**: 58,251, which is 47,342 above the harness,
about 190 MB of freshly touched 4 KB pages.

The cause is the hash table growing. It starts at 2^16 slots and doubles 6
times to 2^21 (24 bytes per slot, so the final table is 48 MB). Each rebuild
allocates a new array and copies the live orders into it. glibc serves blocks
this large with `mmap` and returns them on `free`, so every new array is brand
new zeroed memory that faults page by page. That is about 100 MB per book
(1.5 + 3 + 6 + 12 + 24 + 48 MB), and each process creates two books (two
passes), which matches the ~190 MB above.

A and B allocate many small nodes from the normal heap, and glibc keeps that
heap after `free`, so the second book reuses the first one's pages. Only
about 9 to 10 k faults are their own.

We confirmed this with a pre-sized table (`rebuild(1 << 21)` at construction,
[results/experiments.txt](results/experiments.txt)). Page faults dropped
from 58,251 to 35,509, instructions dropped by 82 M (no re-insert work), and
cache misses dropped by 2 M.

This also shows C's memory trade-off. For ~500 k live orders, the table uses
48 MB because it stays at most half full and the slots are 24 bytes. By our
estimate (one 48 to 64 byte malloc chunk per node, plus A's bucket array), A and B
need roughly 25 to 35 MB for the same orders. C buys speed with memory.

## 11.5 Production choice

We would deploy **C**, with the changes listed under 11.6 (pre-sized table and a
bitmap for finding the next level).

- **Latency**: lowest add and cancel at p50 and at p99. Cancel p99 is 683 ns, vs
  1,467 for A and 2,612 for B.
- **Predictability**: no allocator calls on the hot path, the lowest p99 for both
  add and cancel, and by far the smallest cancel tail. The one real problem is the growth rebuild:
  the call that triggers the largest rebuild re-inserts ~440 k orders into a
  freshly mapped 48 MB array. In a quick separate test that timed every call,
  the worst single add took 20 to 50 ms. It happens only 6 times per run, so
  p99 never shows it, but it is exactly the kind of spike a trading system
  cannot accept. Pre-sizing the table to the expected
  maximum number of orders at startup removes it (and the page faults with it).
- **Memory**: the level array is fixed at 80 KB. The order table is larger than
  A's or B's node memory for the same orders, but it is one allocation of known
  size with no fragmentation, which is easier to plan for than millions of
  small nodes.
- **Cache behaviour**: one cold access per order operation. Everything else is
  contiguous and hot.
- **Complexity / maintainability**: this is C's weak point. The hash table
  (tombstones, rebuild, probing) is our own code and easier to get wrong than
  `std::map`. The `--verify` replay against a naive reference covers that, and
  the whole class is about 150 lines. It also relies on the fixed price band. A
  real instrument whose price drifts would need the array re-centred.

A is the easiest to read and change, so we would keep it as the reference in
tests. B is the one we would not ship: worst cancel, most branch and cache misses,
and no real advantage except slightly faster depth queries than A.

## 11.6 What we would try next

1. **Pre-size the order table.** Measured above: no rebuild stalls, 40% fewer
   page faults.
2. **16-byte slots** (`int32` quantity and tick instead of `long` + `int`), so
   four slots fit in a cache line and the table is a third smaller.
3. **One probe on add instead of two**: `add_order` currently calls
   `find_slot` (duplicate check) and then `insert_slot`, which walk the same
   slots twice.
4. **A two-level bitmap of non-empty ticks**, so finding the next best price and
   walking depth are a few `__builtin_ctzll` calls instead of a linear scan. That
   bounds C's worst case on a sparse book.
5. **An integer tick API.** `llround` alone is 0.6 to 1.0% of samples in the
   profiles, and it is pure conversion cost.
6. **No `std::vector` return from `get_bids` / `get_asks`.** Let the caller pass a
   fixed buffer, which removes an allocation every depth query.
7. **Huge pages** for the order table, to cut the remaining dTLB misses.
8. For A and B: a pool allocator (`std::pmr::unsynchronized_pool_resource`) to
   avoid malloc per node, and a branchless binary search in B.
9. Re-measure on an idle Linux machine with the process pinned to one core, to
   get tighter timing numbers.

## Appendix: perf stat output

<details>
<summary>Implementation A</summary>

```
 Performance counter stats for './bench --impl A --runs 1' (5 runs):

           1269.40 msec task-clock:u                                                            ( +-  9.31% )
        2748580799      cycles:u                                                                ( +-  5.03% )
        1581444010      instructions:u                                                          ( +-  0.07% )
         311154040      branches:u                                                              ( +-  0.07% )
          18328853      branch-misses:u                                                         ( +-  0.34% )
          12057321      cache-misses:u                                                          ( +-  1.35% )
          42099356      L1-dcache-load-misses:u                                                 ( +-  2.09% )
           2043388      dTLB-load-misses:u                                                      ( +-  4.88% )
             20017      page-faults:u                                                           ( +-  0.00% )

       1.274350500 +- 0.117683251 seconds time elapsed  ( +-  9.23% )
```
</details>

<details>
<summary>Implementation B</summary>

```
 Performance counter stats for './bench --impl B --runs 1' (5 runs):

           1690.01 msec task-clock:u                                                            ( +-  9.23% )
        3461375581      cycles:u                                                                ( +-  4.09% )
        2079909357      instructions:u                                                          ( +-  0.02% )
         439822754      branches:u                                                              ( +-  0.02% )
          25965797      branch-misses:u                                                         ( +-  0.15% )
          15251491      cache-misses:u                                                          ( +-  1.82% )
          53845357      L1-dcache-load-misses:u                                                 ( +-  2.86% )
           2744387      dTLB-load-misses:u                                                      ( +-  5.93% )
             20756      page-faults:u                                                           ( +-  0.00% )

       1.699708135 +- 0.156470471 seconds time elapsed  ( +-  9.21% )
```
</details>

<details>
<summary>Implementation C</summary>

```
 Performance counter stats for './bench --impl C --runs 1' (5 runs):

            569.58 msec task-clock:u                                                            ( +-  5.33% )
        1167325943      cycles:u                                                                ( +-  3.14% )
         982862635      instructions:u                                                          ( +-  0.09% )
         161706434      branches:u                                                              ( +-  0.10% )
           5051985      branch-misses:u                                                         ( +-  0.59% )
           6935504      cache-misses:u                                                          ( +-  4.08% )
          14683454      L1-dcache-load-misses:u                                                 ( +-  1.46% )
           1378136      dTLB-load-misses:u                                                      ( +-  2.16% )
             58251      page-faults:u                                                           ( +-  0.00% )

       0.574453145 +- 0.030572608 seconds time elapsed  ( +-  5.32% )
```
</details>
