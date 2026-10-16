# Report – Low-Latency Order Book

**Name:** `Payel Manna`   
**Roll no.:** `24BCS10400`   
**SST email:** `payel.24bcs10400@sst.scaler.com`  


**Machine:** HP Laptop 15s-fq4xxx, Ubuntu 24.04.4 LTS  
**CPU:** 11th Gen Intel(R) Core(TM) i5-1155G7 @ 2.50GHz  
**Cores / Threads:** 4 physical cores / 8 logical threads  
**RAM:** 8 GB (7.4 GiB reported by Linux)  
**Swap:** 4 GB  
**Compiler:** g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1)  
**Build flags:** `-std=c++17 -O2`


**perf setup:** `kernel.perf_event_paranoid` was 4 by default (no counters could be opened); after `sudo sysctl -w kernel.perf_event_paranoid=-1`
all nine requested counters were available (none missing). Counters come from
`perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X --runs 1 --no-op-timing`.
**Caveat:** perf counts the whole process, i.e. also the parts that are identical for A, B and C: generating the 1,000,000-op workload,
process start-up and freeing the book at exit. For C this baseline is large: its process `task-clock` is 271 ms while its order-book loop takes only ~91 ms,
so roughly two thirds of C's counter values are shared overhead. The counters therefore **understate** the real difference between the designs, and
absolute values must not be read as "the cost of the order book alone". (The total-time and ns/op figures come from the benchmark's own timers and do not have this problem.)

---
## 1. Designs

Each order book has to solve two separate problems (Hint 4): *per-order storage* (id → order, needed for cancel) and *per-price
storage* (price → total quantity, needed for BBO and depth). Each implementation is therefore a combination of two structures, and the three
designs differ in **all four** aspects the assignment asks about.

| | A | B | C (own design) |
|---|---|---|---|
| 1. Data structures | `std::map` + `std::unordered_map` | sorted `std::vector` + `std::map` | array `level_[10001]` + `std::vector<Order>` + cached best ticks |
| 2. How orders are stored | hash table, one heap node per order | red-black tree keyed by id, one node per order | array slot per id (`orders_[id]`), contiguous |
| 3. How price levels are stored | one tree node per price holding total qty | contiguous array of `{tick, qty}` sorted by price | one `long` per tick, direct index `tick - 5000` |
| 4. How best bid/ask is found | `begin()` of the map | `back()` of the vector (best kept last) | cached `best_bid_`/`best_ask_`; when the best level empties, walk to the next non-empty tick |
| 5. How an order is found on cancel | hash lookup by id | tree lookup by id | `orders_[id]` direct index |

### Big-O (L = non-empty price levels per side ≤ 5000, N = live orders, d = depth)
| Operation | A | B | C |
|---|---|---|---|
| add_order | O(log L) + O(1) avg hash | O(log N) tree insert + O(log L) search + O(L) shift when a new level is created | O(1) (amortised vector growth) |
| cancel_order | O(log L) + O(1) avg hash | O(log N) + O(log L) + O(L) shift when a level empties | O(1); O(L) walk only when the best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(d) | O(d) | O(d + empty ticks scanned) |
| get_asks | O(d) | O(d) | O(d + empty ticks scanned) |

---
## 2. Correctness
Output of `./verify` (same 1,000,000-op seed-42 workload through A, B, C; also passes with `--ops 200000`):
```
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```
Additionally, `./bench` prints the same checksum for all three implementations (`3176686469504601788`) and the same final BBO
(Bid 99.99 x 2971, Ask 100.01 x 2974). The test also covers: empty book, cancel of unknown / already-cancelled order, same-price
aggregation, insufficient depth (3 levels, ask for 10), invalid prices, qty ≤ 0, duplicate ids.

---
## 3. Results (1,000,000 ops, seed 42)
Times: median of 5 runs from `./bench --impl X` (per-operation times subtract the measured `steady_clock` read overhead, ~50 ns).
Counters: `perf stat -r 5`, mean of 5 runs. IPC = instructions / cycles; branch-miss % = branch-misses / branches.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 598.75 | 772.99 | **90.86** |
| ns / add | 560.2 | 573.4 | **89.6** |
| ns / cancel | 932.8 | 1793.2 | **170.7** |
| ns / BBO | 21.2 | 23.6 | **14.1** |
| IPC | 0.82 | 0.86 | **1.51** |
| Branch misses (%) | 5.43 | 5.54 | **2.13** |
| L1-dcache-load-misses | 21,892,632 | 28,402,626 | **5,363,202** |
| Cache misses | 7,149,724 | 10,006,422 | **2,797,925** |
| Page faults | **23,538** | 24,012 | 24,325 |

Other counters (perf stat, whole process):

| | A | B | C |
|---|---|---|---|
| task-clock (ms) | 917.51 | 1,128.08 | 271.01 |
| cycles | 1,115,192,133 | 1,384,125,428 | 344,596,156 |
| instructions | 909,267,303 | 1,192,021,410 | 519,462,214 |
| branches | 162,394,028 | 230,145,187 | 76,722,941 |
| branch-misses | 8,812,682 | 12,753,618 | 1,633,272 |
| dTLB-load-misses | 914,675 | 1,213,312 | 82,654 |
| peak memory (`/usr/bin/time -v`) | 64,292 KB | 66,400 KB | 67,504 KB |

C is 6.6× faster than A and 8.5× faster than B on total time; on cancel 5.5× and 10.5×; on add 6.3× and 6.4×.
*Caveat:* `get_bbo` takes only a few ns, below the ~50 ns clock overhead that was subtracted, so the BBO numbers have low precision;
only "C is clearly lowest" is meaningful, A and B are indistinguishable.

---
## 4. Performance analysis

### 4.1 Which implementation is fastest?
**C is fastest for all three operations** (measured): add 89.6 ns, cancel 170.7 ns, BBO 14.1 ns, and 91 ms total versus 599 ms (A) and
773 ms (B). An add in C is a bounds check, an array store for the order, an array increment for the level and one compare against the cached best
price; a cancel is one array read by id plus one array update. No searching, hashing or allocation happens. perf agrees: C has the highest
IPC (1.51 vs 0.82 / 0.86) and executes far fewer instructions (519 M vs 909 M / 1,192 M, even though each figure includes the same shared baseline).
**B is the slowest, driven by cancel** (1793 ns, 1.9× A's 933 ns); for add, A and B are about equal (560 vs 573 ns).
My explanation, from the code together with the counters: B keeps ~500k live orders in a `std::map`, so every cancel starts with a ~19-level tree
descent through scattered heap nodes before it even reaches the level vector, whereas A needs about one or two pointer hops in its hash table; B then also
binary-searches the level vector. B accordingly has the most L1, cache, dTLB and branch misses of the three (below). On add, B's contiguous level vector is cheaper than A's level
tree, which offsets its more expensive order-id tree insert, hence the tie.

### 4.2 Branch prediction
**B has the most branch misses** (12.75 M, 5.54% of 230 M branches), then A (8.81 M, 5.43% of 162 M), and C the fewest (1.63 M, 2.13% of 77 M).
Cause, from the code: A and B are full of branches that depend on random data. A: the `std::map` descent (`key < node->key` goes left or right unpredictably because prices
are uniform random) and walks along hash-bucket chains of varying length. B has two such structures per operation: the `std::map` descent over order ids (~19 levels)
**and** `lower_bound`'s binary search over the level vector, each step a data-dependent compare, so it executes the most branches and also mispredicts the most.
C has few data-dependent branches (`valid`, duplicate check, `level empty?`, `t > best_bid_?`); its only variable-length loop is the walk to the next best level.
Part of C's remaining 1.63 M misses is not the order book: the benchmark's dispatch on the random 70/20/10 operation type and the workload generator mispredict in every implementation.

### 4.3 Cache behaviour
**B has the most cache misses** (10.0 M cache-misses, 28.4 M L1-dcache-load-misses, 1.21 M dTLB misses), then A (7.15 M / 21.9 M / 0.91 M), and C
the fewest (2.80 M / 5.36 M / 0.083 M). Relative to C, A has 2.6× the cache-misses, 4.1× the L1 misses and 11× the dTLB misses; B has 3.6×, 5.3× and 14.7×.
* **A and B follow pointers and allocate many small objects:** ~500k live order nodes (and, in A, tree nodes for levels) are scattered over
  the heap, so one operation touches several unrelated cache lines and several different 4 KB pages (hence the dTLB misses). B's order tree is the worst offender.
* **B's level vector is contiguous**, which helps, but it is small and hot anyway; the cost is the order tree.
* **C uses contiguous memory:** the ~80 KB level array stays in cache and an order lookup is a single indexed load (cancels pick random ids, so roughly one miss each).
  IPC below 1 for A and B means the CPU spends much of its time stalled waiting for memory; C reaches 1.5.

### 4.4 Page faults
perf and `/usr/bin/time` agree: A 23,538 (lowest), B 24,012, C 24,325 (highest); peak memory 64.3 / 66.4 / 67.5 MB. All three are within 3.3% of each other, so the node-based
designs do **not** fault noticeably more here. Most memory is common to all three: the benchmark pre-generates 1,000,000 operations (32 bytes each ≈ 32 MB, in a vector that
grows by doubling) and ~500k live orders are held. C's slightly higher count is most likely its order vector growing by doubling up to ~1M entries × 16 B ≈ 16 MB (each growth
allocates a new block and copies), whereas A and B grow through many small allocations the allocator can reuse. So page faults here reflect growth strategy rather than steady-state
size; **pre-allocating** (`reserve`) the tables would remove most of C's faults.

### 4.5 Production choice
I would deploy **C**, with changes before it goes near production:
* **Latency, cache behaviour, predictability:** by far the lowest latency (5–10× on cancel) with the best counters: highest IPC, 2.5–3.5× fewer cache misses, 4–5× fewer L1 misses and
  about 5–8× fewer branch misses than A and B, even with the shared baseline diluting those ratios. It is also the most predictable: O(1) add/cancel, no per-order allocation, no tree rebalancing,
  a small hot data set. A and B pay for allocator calls and pointer chasing, so their tail latency will be worse than their averages suggest.
* **Memory:** similar to the others (67.5 MB vs 64–66 MB here, mostly the benchmark workload); pre-allocation would make it fixed and bounded.
* **Complexity / maintainability:** A is the simplest and most forgiving. C is still short (≈ 70 lines) but encodes two assumptions: a fixed 0.01 tick range, and **small, dense order ids**
  (ids ≥ 2^24 are rejected). Real exchange ids are often sparse 64-bit values, so in production I would map them to dense internal slot numbers (or use a flat hash table) and `reserve` up front.
* **Risk:** after the best level empties, C walks to the next non-empty tick: O(L) worst case (up to 5,000 ticks). Fine for a dense book, but a bitset of non-empty levels would bound it.
  If the price range or id space could not be bounded, A is the safe fallback because it makes no such assumptions.

### 4.6 What would I try next?
* Measure the order book in isolation (pre-generate the workload outside the measured process or subtract a "workload only" baseline run) so perf counters show only the book's own cost.
* Integer ticks through the whole API: `__llround` alone is 3.0% of C's samples (Appendix D), so removing the double→tick conversion is a measurable win.
* Pre-allocate (`reserve`) the order tables and use an object pool instead of per-order `new`, removing growth and page-fault spikes.
* Bitset (or hierarchical bitset) of non-empty levels in C so the next best price is found in O(1).
* Flat open-addressing hash for ids so sparse 64-bit ids stay fast; shrink records (32-bit qty/tick) for better cache density.
* Reduce branches with branchless level updates; pin the benchmark to one core and disable frequency scaling for steadier timings.

---
## Appendix A – raw benchmark output (`results/bench_A.txt`, `bench_B.txt`, `bench_C.txt`)
```
./bench --impl A   (clock overhead 48.9 ns)
Total time (ms) : 598.75 | ns/add 560.2 | ns/cancel 932.8 | ns/get_bbo 21.2
Final BBO: Bid 99.99 x 2971, Ask 100.01 x 2974   Checksum: 3176686469504601788

./bench --impl B   (clock overhead 47.9 ns)
Total time (ms) : 772.99 | ns/add 573.4 | ns/cancel 1793.2 | ns/get_bbo 23.6
Final BBO: Bid 99.99 x 2971, Ask 100.01 x 2974   Checksum: 3176686469504601788

./bench --impl C   (clock overhead 52.1 ns)
Total time (ms) : 90.86 | ns/add 89.6 | ns/cancel 170.7 | ns/get_bbo 14.1
Final BBO: Bid 99.99 x 2971, Ask 100.01 x 2974   Checksum: 3176686469504601788
```
## Appendix B – perf stat (`-r 5`; full output in `results/perf_stat_A.txt`, `perf_stat_B.txt`, `perf_stat_C.txt`)
```
A:   917.51 msec task-clock | 1,115,192,133 cycles | 909,267,303 instructions | 162,394,028 branches | 8,812,682 branch-misses
     7,149,724 cache-misses | 21,892,632 L1-dcache-load-misses | 914,675 dTLB-load-misses | 23,538 page-faults | 0.929 s elapsed
B: 1,128.08 msec task-clock | 1,384,125,428 cycles | 1,192,021,410 instructions | 230,145,187 branches | 12,753,618 branch-misses
    10,006,422 cache-misses | 28,402,626 L1-dcache-load-misses | 1,213,312 dTLB-load-misses | 24,012 page-faults | 1.143 s elapsed
C:   271.01 msec task-clock |   344,596,156 cycles |   519,462,214 instructions |  76,722,941 branches |  1,633,272 branch-misses
     2,797,925 cache-misses |  5,363,202 L1-dcache-load-misses |    82,654 dTLB-load-misses | 24,325 page-faults | 0.275 s elapsed
```
## Appendix C – memory and page faults (`/usr/bin/time -v`)
```
A: Maximum resident set size (kbytes): 64292   Minor page faults: 23552
B: Maximum resident set size (kbytes): 66400   Minor page faults: 24027
C: Maximum resident set size (kbytes): 67504   Minor page faults: 24338
```
## Appendix D – perf record / perf report (`perf record -g ./bench --impl X --runs 1 --no-op-timing`, `perf report --no-children --no-inline -g none`)
Full text in `results/perf_report_A.txt`, `perf_report_B.txt`, `perf_report_C.txt`. Symbol names shortened; percentages are shares of the process's cycle samples.
`add_order`, `get_bbo` and the `std::map` operations are inlined by the compiler into `run_once<...>` (the benchmark loop), so `run_once` = the order-book work itself.
No `clock_gettime`/vdso samples appear any more (they did before the benchmark fix).

```
A (1.11 G cycles)                                 B (1.41 G cycles)                                    C (0.36 G cycles)
36.66%  run_once<OrderBookA> (inlined book code)  56.17%  run_once<OrderBookB> (inlined book code)    22.08%  make_workload
17.27%  OrderBookA::cancel_order                   5.29%  _Rb_tree_rebalance_for_erase                9.19%  mt19937::operator()
 6.16%  make_workload                              5.28%  make_workload                               17.40%  run_once<OrderBookC> (inlined book code)
 4.59%  unordered_map::_M_rehash                   3.01%  _Rb_tree_insert_and_rebalance                8.84%  memmove
 3.27%  malloc_consolidate                         2.83%  memmove                                       3.03%  __llround
 2.92%  unordered_map::erase                       2.72%  mt19937::operator()                           2.55%  mt19937::_M_gen_rand
 2.69%  mt19937::operator()                        2.46%  _Rb_tree::_M_erase                            1.45%  vector<Order>::_M_fill_insert
 2.57%  unlink_chunk                               2.40%  malloc_consolidate                           ~7.4%   kernel samples (page faults etc.)
 2.12%  memmove                                    1.85%  unlink_chunk
 1.92%  _int_malloc                                1.83%  _int_malloc
 1.74%  malloc                                     1.37%  malloc
 1.53%  _int_free                                  1.27%  _Rb_tree::_M_emplace_hint_unique
                                                   1.06%  cfree
```
**What the profiles show**
* **A:** about 54% of the time is the order-book work itself (`run_once` 36.7% + `cancel_order` 17.3%). On top of that, ~11% goes to the memory allocator
  (`malloc`, `_int_malloc`, `_int_free`, `malloc_consolidate`, `unlink_chunk`: one heap node per order) and ~7.5% to the hash table (`_M_rehash` 4.6% while it grows to ~500k orders, `erase` 2.9%).
  This is the cost of node-based storage: allocation, rehash and pointer chasing.
* **B:** `run_once` alone is 56%, because the `std::map` lookup and the binary search over the level vector are inlined into it. The red-black-tree maintenance
  (`_Rb_tree_rebalance_for_erase`, `_Rb_tree_insert_and_rebalance`, `_M_erase`, `_M_emplace_hint_unique`) adds another ~12%, and the allocator ~8.5%. Rebalancing on every order insert and erase is work that A's hash table and C's array simply do not have,
  which matches B having the slowest cancel.
* **C:** the order book is a small slice of the profile: `run_once` is only 17.4% (plus `__llround` 3.0% and `vector<Order>::_M_fill_insert` 1.5%). The biggest costs are **not the order book**:
  workload generation (`make_workload` 22.1% + `mt19937` 11.7% ≈ 34%) and `memmove` (8.8%, most likely the pre-generated operation vector growing by doubling). This confirms the caveat in section 3: for C most of the
  process's cycles and counters are shared baseline, so the real difference to A and B is larger than the perf counters alone suggest. There are no allocator or tree functions at all.
* **Optimisation hint from C's profile:** `__llround` (double → tick conversion on every add) is 3% of all samples, roughly one sixth of the order-book loop's own 17.4% share. Passing integer ticks through the API would remove it.