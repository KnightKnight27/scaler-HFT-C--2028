# Order Book Capstone: Results and Performance Analysis

**Varun Reddy** · varun.24bcs10009@sst.scaler.com · 24BCS10009  
**Aniruddha Patil** · Aniruddha.23bcs10156@sst.scaler.com · 23BCS10156

**Machine:** AMD Ryzen 9 5900HX (Zen 3; 32 KiB L1d and 512 KiB L2 per core, 16 MiB L3),
Ubuntu 24.04, Linux 7.0, g++ 13.3 `-O2`, CPU governor `performance`, benchmark pinned to
one core (`taskset -c 2`). The designs are described in [README.md](README.md#data-structure-choices).

## 1. Results table

Benchmark: `./build/bench --impl X` (5 repetitions, median; [results/bench_*.txt](results/)).
Counters: `perf stat -r 5 … ./build/bench --impl X --reps 1 --no-latency` (§4).

| Metric | A: map + unordered_map | B: sorted vectors | C: array + bitmap + flat hash |
|---|---:|---:|---:|
| **Total time (ms)** | 195.3 | 164.5 | **43.7** |
| **ns / add** | 192.7 | 131.2 | **92.6** |
| **ns / cancel** | 382.4 | 361.1 | **90.3** |
| **ns / BBO** (per op, in the mixed workload) | 24.9 | 26.5 | **20.2** |
| ns / BBO (tight loop of 10M calls) | 3.23 | 3.33 | **3.12** |
| ns / depth query (bids(10) + asks(10)) | 225.2 | 210.3 | **181.0** |
| add p99.9 / max (ns) | 1,262 / 10,471,267 | 2,675 / 11,670,147 | **601 / 29,414** |
| cancel p99.9 / max (ns) | 2,084 / 25,587 | 2,485 / 30,887 | **701 / 25,627** |
| **IPC**, whole process (book only†) | 0.63 (0.49) | 0.79 (0.60) | **0.96 (0.62)** |
| **Branch misses (%)**, whole process (book only†) | 7.2% (7.4%) | **12.0% (14.6%)** | 5.6% (4.5%) |
| **L1-dcache-load-misses** | 23.5 M | **42.0 M** | 7.1 M |
| **Cache misses** (on Zen 3: L2 misses) | 9.53 M | **18.84 M** | 2.19 M |
| dTLB-load-misses | 827 k | 550 k | **914 k** |
| **Page faults**, whole process | 16,515 | **19,349** | 13,210 |
| Page faults while building the book (cold heap) | 7,228 | **10,063** | 4,098 |
| Heap held by the book at the end | 29.0 MB | 24.3 MB | 16.1 MB |

† *Book only* = implementation minus the `--impl none` baseline, which generates the same
workload but runs no book (65 ms, 203 M cycles, 282 M instructions, 9,093 page faults).
Generating 1M random operations is a fixed cost in every process. Without subtracting it,
C's counters would be roughly half harness.

**Counter availability:** all nine requested counters worked. Zen 3 has fewer
general-purpose counters than the seven hardware events requested, so perf
time-multiplexed them (each was live about 71% of the time and scaled up). Variation
across the 5 runs was ≤ 2% for every counter except C's cycles (±5%) and B's
dTLB misses (±8%).

**How to read the latency numbers:** the per-op figures time each call individually
with `lfence`, which stops the CPU from overlapping the cache miss of one operation
with the next. They therefore measure **latency**. The untimed total gives
**throughput**: C does 1.01M operations in 43.7 ms, about 43 ns per operation, half its
timed per-op latency, because out-of-order execution overlaps independent misses.
The TSC on this CPU ticks in ~10 ns steps, so single BBO samples (20–30 ns) mostly
measure the timer. The tight-loop row shows the real BBO cost: about 3 ns per call in
all three, mostly the function call itself.

## 2. Where the time goes (`perf record -g`)

Self time per function, top entries ([results/perf_report_*.txt](results/)):

| A | | B | | C | |
|---|---:|---|---:|---|---:|
| `OrderBookMap::add_order` (inlined tree + hash insert) | 23.4% | `OrderBookVector::find_order` (binary search by id) | 11.2% | `OrderBookArray::add_order` (inlined hash insert) | 30.5% |
| `OrderBookMap::cancel_order` | 11.2% | `SortedLevels<greater>::find_pos` (bid level search) | 10.5% | `OrderBookArray::cancel_order` | 3.3% |
| `_Hashtable::_M_rehash` | **9.7%** | `SortedLevels<less>::find_pos` (ask level search) | 9.8% | `OrderBookArray::get_bbo` | 1.3% |
| `free` + `malloc_consolidate` + `_int_free` + `unlink_chunk` | **14.4%** | `memmove` (vector insert/erase/regrow) | 4.9% | | |
| `malloc` + `_int_malloc` | 4.3% | `OrderBookVector::add_order` | 4.2% | | |
| workload generation (mt19937, generator) | ~10% | workload generation | ~16% | workload generation | ~25% |

In A, about **19% of all cycles are spent in the allocator**: one heap node per order
plus one per new price level, all freed again at teardown. Another **10% goes to
rehashing** the `unordered_map` as it grows to 500k entries. In B, three binary
searches account for a third of the run. In C, nothing but the book itself and the
workload generator shows up.

## 3. Analysis

### 3.1 Which implementation is fastest?

**C is fastest for every operation**: 4.5× faster than A in total time and 3.8× faster
than B. The margin is largest for **cancel**: 90 ns against 361–382 ns.

- **add_order.** C does one hash computation, one probe into a pre-sized table (usually
  a single cache line, at load factor ≤ 0.48), and an indexed add into a 80 KB array that
  stays in L2. A does two dependent pointer-chasing searches (`unordered_map` bucket
  then node, and a ~12-level red-black tree descent for a new or existing level) plus a
  `malloc` per order. B is respectable at **add** (131 ns, p50 of only 80 ns): ids
  increase, so its order store is a sequential `push_back`, the most cache-friendly
  write possible, and its level search runs over an 80 KB vector that also fits in L2.
- **cancel_order.** This is where B pays. The live order is found by binary search over
  a 700k-entry, 16.8 MB vector. That is about 20 *dependent* loads, and the last ~10
  each land on a different cache line far apart in memory, so they miss L1 and L2.
  A's cancel is slow because the order's node lives somewhere random in the heap
  (cache miss), the tree descent to its level adds more dependent misses, and then two
  `free` calls follow. C's cancel is one hash probe that returns the tick index, then a
  direct array write. It does the same work as an add, which is why the two have
  identical latency.
- **get_bbo.** All three are O(1) and all cost about 3 ns in a tight loop, because the
  best level is always hot there. In the mixed workload C is slightly ahead (20 ns vs
  25–27 ns): its best-price index and quantity array are touched by every add, so they
  stay in L1, whereas A has to dereference the tree's leftmost node and B its vector's
  last element.

### 3.2 Branch prediction

**B has the most branch misses**: 12.0% of all branches, and 14.6% of the book's own
branches (10.8 M mispredictions). The cause is `std::lower_bound`. Each step of a binary
search branches left or right depending on a comparison with random data, so the
predictor cannot do better than a coin flip. B runs two binary searches per cancel
(by id, ~20 steps; by level, ~12) and one per add (by level, ~12). That comes to about
200k × 32 + 700k × 12 ≈ 15M data-dependent branches, of which roughly half
mispredict, at ~15 cycles each.
A's tree descent has the same left/right problem (7.4%), plus branches in the
hash-chain walk and inside `malloc`/`free`. C has the fewest (4.5%). Its common path
has only well-predicted branches: the slot is usually empty or a match on the first
probe, the level is usually non-empty, and the best price rarely changes. The only
data-dependent branches are "is this a new best?" and the rare "did the best level just
empty?".

### 3.3 Cache behaviour

**B has the most cache misses** (18.8 M L2 misses and 42 M L1 misses), almost twice A's.
That surprised me, because B is the "contiguous" design. Contiguity helps only when
the access pattern is sequential or local, and binary search is neither: its probes
jump by halving distances across 16.8 MB, so a single cancel touches ~10 lines that
are certainly not cached.

- **A** follows pointers everywhere. A hash bucket points to a node, the tree's root
  points down ~12 levels, and every node is a separate small allocation (~48 bytes each,
  500k of them) scattered across the heap. It has fewer misses than B because a lookup
  chases only 2–3 pointers to reach an order, but each one is a dependent miss, and A
  also executes the most instructions: 547 M for the book, against 392 M for B and
  160 M for C.
- **B** reads and writes contiguous memory, which makes adds cheap. Searches, however,
  jump around a large array. Inserting or erasing a level shifts up to 80 KB with
  `memmove`, and the vector's doubling growth copies the whole order array.
- **C** makes the fewest accesses that can miss. An add or cancel touches one hash-table
  line (a random location in a 16 MB table, so it is often a real miss) and one line of
  the 80 KB level array, which stays in L2. No per-order allocation and no pointer
  chasing happens. C does have the most **dTLB misses** (914 k): with 4 KB pages, the
  16 MB table spans 4,096 pages, more than the L2 TLB covers, so random probes often
  miss in the TLB too. Huge pages would fix that (§3.6).

### 3.4 Page faults

**B causes the most page faults** (19,349 for the whole process; 10,063 while building
the book). Its vectors grow by doubling, and every reallocation of the order vector maps
a fresh region (glibc uses `mmap` for blocks this large), writes it once, and unmaps
the old one. That totals about 24+12+6+… ≈ 48 MB of newly touched memory, and during
each copy both the old and new buffers are live, so the peak footprint is 1.5× the data.
B also faults again in every repetition, because the freed `mmap` regions go back to
the OS. **A** faults about 7k times on a cold heap as it grows the heap for 500k nodes
and its bucket array, but later repetitions reuse memory that `malloc` kept, so they
show 0. **C** faults exactly once per page of its 16 MB table (4,096 faults) when the
table is zeroed at construction, and never afterwards. Its memory is reserved up front
and its footprint is the smallest (16.1 MB against 24–29 MB). The cost is that it uses
16 MB even for an empty book.

The page-fault cost also explains the worst-case latencies. A's slowest add (10.5 ms)
is a full `unordered_map` rehash of ~500k nodes, which shows up in perf as
`_M_rehash` at 9.7%. B's slowest add (11.7 ms) is the order vector doubling from 12 to
24 MiB (copy plus fresh page faults). C's worst case (29 µs) is the same size as the
worst case of a trivial BBO query (up to 29 µs in B), so it is OS noise (interrupts),
not the data structure.

### 3.5 Production choice

I would deploy **C**, with its preallocation sized for the worst expected day.

- **Latency:** it is the lowest for every operation, and it is the only design where
  cancel costs no more than add.
- **Predictability:** this matters more than the average. C's p99.9 is 0.6–0.7 µs, and
  nothing in it can stall: no rehash (the table is pre-sized), no `malloc`, no vector
  regrowth, no `memmove`, and finding the next best price is O(1) through the bitmap
  even in a sparse book. A and B both have *millisecond* stalls built into their designs
  (rehash, reallocation), which a trading system cannot afford at an unpredictable
  moment.
- **Memory:** fixed and known at start-up (16 MB + 80 KB), with no fragmentation. The
  downside is paying for the capacity even when the book is empty.
- **Cache behaviour:** about one cache line per operation for the order, plus a level
  array that stays in L2.
- **Complexity and maintainability:** this is C's real cost. A is about 100 lines of
  obviously-correct standard containers, while C has a hand-written hash table
  (backward-shift deletion is easy to get wrong; it is one of the bugs my verifier was
  shown to catch) and a bitmap. It also assumes a known, bounded price band, so a real
  instrument would need the array re-centred around a moving price window. I would
  accept that cost only together with the differential test against a simple reference
  model, run in CI. **A** would be my choice for a prototype or a non-latency-critical
  system. I would not deploy **B**: its average add is good, but its cancels are slow,
  it has the most branch and cache misses, it has millisecond regrowth stalls, and
  out-of-order ids degrade it to O(N).

### 3.6 What I would try next

1. **Huge pages for the order table** (`madvise(MADV_HUGEPAGE)` or a 2 MB-aligned
   allocation). This should remove most of C's dTLB misses, its largest remaining
   counter.
2. **Software prefetch**: when the workload is known a few operations ahead (e.g. a
   batch of messages from the network), prefetch the hash slot for message i+4 while
   processing message i, which turns latency into throughput.
3. **Integer-tick API**: `add_order(id, side, int32_t tick, qty)` would skip the
   `double` → tick conversion and its `nearbyint`/`fabs` checks on every add.
4. **No allocation in `get_bids`/`get_asks`**: fill a caller-provided buffer or
   `std::array<Level,10>` instead of returning a `std::vector`. Today the depth query
   is dominated by two `malloc`s (~180–225 ns in every design).
5. **Smaller level array**: `int32` quantities would halve it to 40 KB and nearly fit
   in L1d.
6. **Fix A and B with the same ideas**: `reserve()` the `unordered_map` and give it a
   pool allocator (`std::pmr::unsynchronized_pool_resource`), which removes the rehash
   stall and most allocator time. For B, use a branchless binary search (conditional
   moves) to cut its branch misses, and look orders up directly by
   `id - first_id` when ids are dense.
7. **Per-order FIFO queues at each level** (intrusive linked list in a pool), which a
   real exchange book needs for time priority once matching is added.
8. **Build flags and tooling**: `-march=native` and profile-guided optimisation
   (`-fprofile-use`), plus a bigger, realistic workload (prices clustered near the
   inside, replayed market data) to check whether the ranking changes.

## 4. perf stat output

Command: `perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults taskset -c 2 ./build/bench --impl X --reps 1 --no-latency`


### Implementation A

```
Performance counter stats for 'taskset -c 2 ./build/bench --impl A --reps 1 --no-latency' (5 runs):

            398.18 msec task-clock                                                              ( +-  1.17% )
     1,326,510,404      cycles                                                                  ( +-  0.84% )  (71.25%)
       829,796,197      instructions                                                            ( +-  0.25% )  (71.24%)
       153,019,273      branches                                                                ( +-  0.17% )  (71.29%)
        11,051,850      branch-misses                                                           ( +-  0.27% )  (71.54%)
         9,525,360      cache-misses                                                            ( +-  1.85% )  (71.75%)
        23,527,708      L1-dcache-load-misses                                                   ( +-  0.68% )  (71.59%)
           827,016      dTLB-load-misses                                                        ( +-  1.01% )  (71.35%)
            16,515      page-faults                                                             ( +-  0.00% )

       0.402370719 +- 0.004618276 seconds time elapsed  ( +-  1.15% )
```

### Implementation B

```
Performance counter stats for 'taskset -c 2 ./build/bench --impl B --reps 1 --no-latency' (5 runs):

            270.04 msec task-clock                                                              ( +-  1.50% )
       854,207,945      cycles                                                                  ( +-  1.03% )  (70.97%)
       674,318,763      instructions                                                            ( +-  0.28% )  (71.24%)
       108,208,068      branches                                                                ( +-  0.15% )  (71.63%)
        13,013,013      branch-misses                                                           ( +-  0.27% )  (71.72%)
        18,838,311      cache-misses                                                            ( +-  1.01% )  (71.71%)
        41,965,584      L1-dcache-load-misses                                                   ( +-  0.46% )  (71.54%)
           550,431      dTLB-load-misses                                                        ( +-  8.45% )  (71.18%)
            19,349      page-faults                                                             ( +-  0.00% )

       0.272633984 +- 0.004104321 seconds time elapsed  ( +-  1.51% )
```

### Implementation C

```
Performance counter stats for 'taskset -c 2 ./build/bench --impl C --reps 1 --no-latency' (5 runs):

            142.96 msec task-clock                                                              ( +-  6.17% )
       462,408,864      cycles                                                                  ( +-  5.28% )  (70.84%)
       442,935,965      instructions                                                            ( +-  0.44% )  (70.87%)
        64,539,131      branches                                                                ( +-  0.19% )  (71.41%)
         3,590,157      branch-misses                                                           ( +-  0.49% )  (71.91%)
         2,186,735      cache-misses                                                            ( +-  3.06% )  (71.93%)
         7,140,217      L1-dcache-load-misses                                                   ( +-  1.02% )  (71.80%)
           914,142      dTLB-load-misses                                                        ( +-  1.99% )  (71.24%)
            13,210      page-faults                                                             ( +-  0.00% )

       0.144302060 +- 0.008844982 seconds time elapsed  ( +-  6.13% )
```

### Baseline (`--impl none`: workload generation only)

```
Performance counter stats for 'taskset -c 2 ./build/bench --impl none --reps 1 --no-latency' (5 runs):

             65.21 msec task-clock                                                              ( +-  1.65% )
       203,018,380      cycles                                                                  ( +-  1.58% )  (71.22%)
       282,499,145      instructions                                                            ( +-  0.21% )  (72.02%)
        34,244,820      branches                                                                ( +-  0.56% )  (71.99%)
         2,225,080      branch-misses                                                           ( +-  0.90% )  (71.99%)
           741,473      cache-misses                                                            ( +-  0.52% )  (71.99%)
         3,494,462      L1-dcache-load-misses                                                   ( +-  0.58% )  (70.80%)
            40,492      dTLB-load-misses                                                        ( +-  2.71% )  (69.99%)
             9,093      page-faults                                                             ( +-  0.01% )

       0.066648160 +- 0.001047451 seconds time elapsed  ( +-  1.57% )
```

## 5. Correctness output

```
Running correctness test...

[1/3] Edge-case checks (31 per implementation): A ok, B ok, C ok
[2/3] benchmark workload: 1010000 ops (700471 add, 199305 cancel, 100224 bbo, 10000 depth), 4041010 checks per implementation: A ok, B ok, C ok
[3/3] stress workload: 329438 ops (146662 add, 155827 cancel, 15050 bbo, 11899 depth), 1318082 checks per implementation: A ok, B ok, C ok

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```
