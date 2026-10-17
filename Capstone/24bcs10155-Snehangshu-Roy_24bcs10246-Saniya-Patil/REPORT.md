# Order Book Capstone - Report

Snehangshu Roy (24BCS10155), Saniya Patil (24BCS10246)

## 1. The three implementations

All three books store prices as integer ticks (100.01 -> 10001). The qty of a level is the sum
of its orders' qtys, and every order has qty >= 1, so a level with qty 0 has no orders left. None
of the books keep the individual orders inside a level: there's no matching in this assignment,
so nothing ever needs the order of orders within a level, only the total.

### A - std::map + std::unordered_map

- Orders: `std::unordered_map<uint64_t, Order>` with `Order = {side, tick, qty}`, one heap node per order.
- Price levels: one `std::map<int, long>` per side (a red-black tree, one node per level). Bids use
  `std::greater<int>`, so `begin()` is the best price on both sides.
- Best bid/ask: `begin()`, which `std::map` keeps cached, so O(1).
- Cancel: `unordered_map::find(id)`. libstdc++ uses chained hashing, so that's the bucket array and
  then a pointer to the node with side, tick and qty. Then `map.find(tick)`, subtract, and erase the
  level node if it hits 0. C also hashes the id, but with open addressing in one flat array
  (see 11.3).

| op | complexity |
|---|---|
| add_order | O(1) avg hash insert + O(log P) map lookup/insert = O(log P) |
| cancel_order | O(1) avg hash find + O(log P) map find = O(log P) |
| get_bbo | O(1) |
| get_bids | O(d), walk the bid map from begin |
| get_asks | O(d), walk the ask map from begin |

### B - std::priority_queue with lazy deletion + std::map for orders

- Orders: `std::map<uint64_t, Order>`, a tree keyed by id. We used a tree here on purpose so that
  the cancel lookup is different from A and C (O(log N) instead of a hash).
- Price levels: `std::unordered_map<int, LevelInfo>` per side, `LevelInfo = {qty, in_heap}`.
- Best bid/ask: a max-heap of ticks for bids and a min-heap for asks. A heap can't delete from the
  middle, so when a level empties we leave its tick in the heap ("stale") and only pop it once it
  gets to the top. When a cancel empties the top level we pop stale ticks until the top is live
  again, so `get_bbo()` just reads `top()`. The `in_heap` flag makes sure a tick is pushed at most
  once, so a heap never has more than 5000 entries.
- Cancel: tree lookup of the id, subtract from the level in the hash map, clean the heap top if needed.
- Depth: the heap isn't sorted, so `get_bids(d)` copies the heap and pops until it has d live levels.

| op | complexity |
|---|---|
| add_order | O(log N) tree insert + O(1) hash + O(log P) heap push (only for a new level) = O(log N) |
| cancel_order | O(log N) tree find/erase + amortized O(log P) for popping stale ticks (each tick is popped once per push) |
| get_bbo | O(1) (heap top + one hash lookup) |
| get_bids | O(P + (d + s) log P): copy the heap, pop until d live levels, s = stale ticks popped on the way, so O(P log P) worst case |
| get_asks | same as get_bids |

### C - our design: flat price array + bitmap + own hash table

- Price levels: one `long qty_[10001]` array indexed by `tick - 5000`. The whole band is only
  10,001 ticks, so we just allocate all of it (80 KB). Index 5000 (100.00) is never used, which
  also keeps bids and asks apart.
- Best bid/ask: cached `best_bid_` / `best_ask_` indexes. On add it's one compare. On cancel, if the
  best level empties we need the next one, so we keep a bitmap `used_[157]` (1 bit per level, ~1.2 KB)
  and scan it 64 levels at a time with `__builtin_clzll` / `__builtin_ctzll`. Worst case is 79 words
  for one side.
- Orders: our own open addressing hash table `OrderTable`, one `std::vector` of 16 byte slots
  `{id, tick, qty}`. Power of 2 size, fibonacci hashing, linear probing, and it doubles before it
  gets more than half full. `tick == 0` marks an empty slot. Erase uses backward shift (later
  entries of the cluster move back into the hole), so there are no tombstones.
- Cancel: the hash lookup gives tick and qty directly (no pointer to follow), then subtract from
  `qty_[i]`. If the level emptied we clear its bit, and we only rescan if it was the best one.

| op | complexity |
|---|---|
| add_order | O(1) amortized (O(N) on the rare resize) |
| cancel_order | O(1) expected, O(P/64) worst case when the best level empties |
| get_bbo | O(1) |
| get_bids | O(d + P/64) worst case |
| get_asks | O(d + P/64) worst case |

(N = live orders, P = price levels per side, at most 5000, d = depth.)

## 2. Setup

- AMD Ryzen 9 8940HX (16 cores / 32 threads, 1 MB L2 per core, 32 MB L3), a laptop
- Windows 11, Docker Desktop with the WSL2 kernel 6.6.87.2, Ubuntu 24.04 container
- g++ 13.3.0, `-std=c++17 -O2 -g -fno-omit-frame-pointer`, perf 6.8.12
- All counters from the assignment were available. There are more events than hardware counters,
  so perf multiplexes them: each one counted ~85% of the time and perf scales it up (the last
  column in the perf stat output).

It's a laptop inside a VM, and a small local kind cluster from another course was idling in the
background (~0.2 of a core), so there's noise. Within one session the `+-` in perf stat is a few %,
but between sessions the medians moved more than that for the slow ops: B's cancel was anywhere
from 312 to 471 ns. C was always the fastest and B's cancel always the slowest. Everything below
comes from one final run of `run_perf.sh`.

## 3. Results

Total time and the ns rows are medians of 5 runs of `./bench --impl X` (`results/bench_X.txt`).
The other rows are from `perf stat -r 5 ... ./bench --impl X` run exactly like in the assignment
(`results/perf_stat_X.txt`).

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 126.2 | 135.7 | 33.4 |
| ns / add | 113.4 | 71.2 | 56.7 |
| ns / cancel | 201.6 | 471.2 | 38.3 |
| ns / BBO | 1.69 | 3.04 | 1.48 |
| IPC | 0.80 | 0.99 | 1.18 |
| Branch misses (%) | 5.84% (93.5M) | 2.57% (58.8M) | 3.05% (20.4M) |
| L1-dcache-load-misses | 199.6M | 318.5M | 43.8M |
| Cache misses | 63.0M | 95.7M | 14.2M |
| Page faults | 19,610 | 18,787 | 25,153 |
| dTLB-load-misses | 9.31M | 16.57M | 5.91M |

Some things to know when reading it:

- One `./bench --impl X` process does a lot more than the pass behind "Total time". It makes the
  ops, measures the clock overhead, and then plays the ops on 11 fresh books (5 runs, each with an
  untimed and a timed pass, plus one replay at the end to print the final book). perf stat counts
  all of that, which is why A's task-clock is 2.0 s while its total time is
  126 ms. The per-run table below is the cost of one pass.
- add and cancel have the ~19 ns cost of one `steady_clock::now()` call subtracted (the raw
  numbers are in `results/bench_X.txt`).
- One `get_bbo` takes less time than a clock read, so it's timed in batches of 16 calls. All 16
  read the same book, which is in L1 after the first call, and the CPU can overlap them, so this
  is for repeated calls with the book already in cache. We didn't measure one call on a cold cache.
- The per-op times add up to more than the total. For C, 0.7 x 56.7 + 0.2 x 38.3 +
  0.1 x 1.5 = 47.5 ns per op, but the untimed loop does 33.4 ns per
  op. We think the clock reads around each op stop the CPU from overlapping one op's cache misses
  with the next op, so per-op is closer to latency and total / ops is closer to throughput.
- A and B are only 8% apart on total time. That's less than either of them moved between our
  sessions (A was 116-142 ms, B 123-153 ms), so we wouldn't read much
  into A being ahead of B.

### Counters for the book alone

perf record showed `__vdso_clock_gettime` taking 10% of A's samples, 9% of B's and
29% of C's (35% with `steady_clock::now` itself). So the timing pass was distorting
the counters, mostly for C. It also changes IPC: C goes from 1.42 with `--no-timers` to
1.18 with the timers on (our guess is that reading the clock waits for the instructions before
it to finish), while A goes from 0.79 to 0.80. So we did two more things:

1. `--no-timers` skips the timed pass and the clock overhead measurement
   (`results/perf_stat_X_no_timers.txt`).
2. `per_run_counters.sh` runs perf stat with `--runs 1` and `--runs 5` (both `--no-timers`) and
   takes (runs5 - runs1) / 4. That's the cost of one pass of the 1M ops on one book, including
   creating and destroying the book. Making the ops, the final replay and process startup cancel
   out (`results/perf_per_run.txt`):

| per run (1M ops) | A | B | C |
|---|---|---|---|
| cycles | 686M | 785M | 142M |
| instructions | 543M | 786M | 200M |
| IPC | 0.79 | 1.00 | 1.41 |
| branches | 118.8M | 175.4M | 33.2M |
| branch-misses | 7.81M (6.6%) | 4.64M (2.6%) | 1.03M (3.1%) |
| cache-misses | 5.33M | 8.35M | 1.20M |
| L1-dcache-load-misses | 17.1M | 28.2M | 3.6M |
| dTLB-load-misses | 0.79M | 1.37M | 0.38M |
| page-faults | 0 | 0 | 0 |

Page faults per run show up as -1, -1 and 0 in the raw file: the averages for `--runs 1` and `--runs 5`
differ by a few faults (out of ~19-25k), so it's really 0 per run. Most of the analysis below uses this table.

### perf stat output

`perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X`

A
```
 Performance counter stats for './bench --impl A' (5 runs):

           2008.35 msec task-clock                       #    0.999 CPUs utilized               ( +-  1.65% )
        9713878441      cycles                           #    4.837 GHz                         ( +-  1.62% )  (85.69%)
        7737737549      instructions                     #    0.80  insn per cycle              ( +-  0.28% )  (85.75%)
        1600419736      branches                         #  796.881 M/sec                       ( +-  0.20% )  (85.76%)
          93480531      branch-misses                    #    5.84% of all branches             ( +-  0.32% )  (85.76%)
          62976263      cache-misses                                                            ( +-  0.60% )  (85.76%)
         199550796      L1-dcache-load-misses                                                   ( +-  1.06% )  (85.76%)
           9309485      dTLB-load-misses                                                        ( +-  1.38% )  (85.52%)
             19610      page-faults                      #    9.764 K/sec                       ( +-  0.01% )

            2.0114 +- 0.0332 seconds time elapsed  ( +-  1.65% )
```

B
```
 Performance counter stats for './bench --impl B' (5 runs):

           2216.04 msec task-clock                       #    0.999 CPUs utilized               ( +-  1.38% )
       10799257929      cycles                           #    4.873 GHz                         ( +-  1.40% )  (85.67%)
       10727640926      instructions                     #    0.99  insn per cycle              ( +-  0.16% )  (85.69%)
        2287309926      branches                         #    1.032 G/sec                       ( +-  0.07% )  (85.67%)
          58764650      branch-misses                    #    2.57% of all branches             ( +-  0.23% )  (85.69%)
          95660928      cache-misses                                                            ( +-  0.43% )  (85.78%)
         318500695      L1-dcache-load-misses                                                   ( +-  0.16% )  (85.87%)
          16573614      dTLB-load-misses                                                        ( +-  1.28% )  (85.63%)
             18787      page-faults                      #    8.478 K/sec                       ( +-  0.00% )

            2.2191 +- 0.0306 seconds time elapsed  ( +-  1.38% )
```

C
```
 Performance counter stats for './bench --impl C' (5 runs):

            713.46 msec task-clock                       #    0.996 CPUs utilized               ( +-  1.02% )
        3466542481      cycles                           #    4.859 GHz                         ( +-  1.31% )  (85.63%)
        4091250093      instructions                     #    1.18  insn per cycle              ( +-  0.59% )  (85.58%)
         667647409      branches                         #  935.785 M/sec                       ( +-  0.33% )  (85.54%)
          20358691      branch-misses                    #    3.05% of all branches             ( +-  1.60% )  (85.62%)
          14162600      cache-misses                                                            ( +-  1.48% )  (85.74%)
          43782149      L1-dcache-load-misses                                                   ( +-  0.94% )  (86.09%)
           5905697      dTLB-load-misses                                                        ( +-  3.95% )  (85.82%)
             25153      page-faults                      #   35.255 K/sec                       ( +-  0.00% )

           0.71629 +- 0.00738 seconds time elapsed  ( +-  1.03% )
```

### perf record

Top of `perf report --no-children` for each (full output in `results/perf_report_X.txt`):

| A | | B | | C | |
|---|---|---|---|---|---|
| `MapBook::add_order` | 33.5% | `HeapBook::cancel_order` | 26.3% | `__vdso_clock_gettime` | 29.3% |
| `MapBook::cancel_order` | 22.6% | `HeapBook::add_order` | 10.8% | `OrderTable::insert` | 23.2% |
| `__vdso_clock_gettime` | 10.3% | `__vdso_clock_gettime` | 8.6% | `run_once<ArrayBook>` (add inlined here) | 11.1% |
| `_Hashtable::_M_rehash` | 6.6% | libc (malloc / free / memcpy) | 7.4% | `play<ArrayBook>` (add inlined here) | 7.0% |
| `run_once<MapBook>` | 6.4% | level `unordered_map::find` | 6.2% | `ArrayBook::cancel_order` | 6.4% |
| libc (malloc / free) | 3.9% | level `unordered_map::operator[]` | 6.0% | `steady_clock::now` | 5.3% |
|  |  | `_Rb_tree_rebalance_for_erase` | 5.7% | `vector<Slot>::_M_fill_assign` | 3.2% |
|  |  | `_Rb_tree::_M_erase` (order map destructor) | 5.2% | `llround` | 2.0% |
|  |  | `_Rb_tree_insert_and_rebalance` | 2.9% | kernel (page fault handling) | 1.7% |

In A the container code is mostly inlined into `add_order` and `cancel_order`. In B only the id
tree walk is inlined. The level hash table and the tree rebalancing show up as their own rows. In C
about half of the time outside the clock is the hash table (`insert`, the probe loop in
`cancel_order`, and zeroing a new table when it grows, which is `_M_fill_assign` plus some kernel
time for the page faults). Most of the rest is `add_order` inlined into `run_once` and `play`. C has no
malloc or free above 0.5% (apart from the table growing, the only allocations left are the vectors returned by `get_bids` /
`get_asks`).

## 4. Performance analysis

### 11.1 Which implementation is fastest?

C is the fastest for add_order, cancel_order and get_bbo. In total time it's about 3.8x faster than A and
4.1x faster than B, and it uses 4.8x fewer cycles per run than A.

add_order: C 56.7 ns, B 71.2 ns, A 113.4 ns. C does one probe into its hash table and
one array update, with no allocation and no tree. We expected A to beat B, but B's order map gets
the ids in increasing order, so every insert goes down the right edge of the tree, which stays in
cache. Both of them malloc a node per order, but A also pays for rehashing as the table grows (`_M_rehash` is
6.6% of A's samples) and for a ~13 level walk in the level `std::map` where every
comparison depends on a random price.

cancel_order: C 38.3 ns, A 201.6 ns, B 471.2 ns, the biggest gap. B looks for a
random id in a tree of up to 500k nodes (about 19 levels, each node somewhere else in ~30 MB of
heap), so most steps are cache misses, and then it rebalances. A's hash lookup only follows two
pointers (bucket, then node), but it still walks the level tree and frees the node. C reads one 16 byte slot
that already has the tick and qty.

get_bbo: C 1.5 ns, A 1.7 ns, B 3.0 ns. Nothing gets searched: A and C read cached values, and B also does a hash
lookup per side for the qty, so it takes about 2x as long. Most of A's and C's time here is
the tick to double division. When our first timing loop didn't use the prices, the compiler
dropped it and C came out at ~0.2 ns.

### 11.2 Branch prediction

A has the most branch misses: 93.5M in the perf stat run, 7.81M per run (6.6% of
its branches). Most come from the level `std::map`. At each node the CPU guesses left or right, and
with random prices that's a coin flip at each of ~13 levels, on every add and every cancel.

B misses less often (2.6%) but still 4.64M times per run. Its cancels do the same
coin flip walk down the id tree (~19 levels), but its adds always go right, which brings the rate
down.

C has the fewest, 1.03M per run, about one per op. We think most of those happen in every
implementation: the op type is random (70/20/10), so the benchmark's `switch` mispredicts a lot,
and in `add_order` the side is a coin flip. The rest is the end of the probing loop, since cluster
lengths vary.

### 11.3 Cache behavior

B has the most cache misses (8.35M per run, 28.2M L1 misses), then A (5.33M /
17.1M), then C (1.20M / 3.6M).

| | A | B | C |
|---|---|---|---|
| contiguous memory? | no | only the heap vector | yes, everything is a flat array |
| jumps around memory? | yes | yes | only for the hash slot |
| follows pointers? | yes: hash chain + tree nodes | yes: ~19 tree nodes per cancel | no |
| many small allocations? | yes: 1 node per order + 1 per level | yes: 1 node per order + 1 per level | no, nothing per op (only when the table grows) |

In A and B every order is a separate small allocation, so the orders are spread all over memory.
In B's tree the CPU only learns where the next node is after loading the current one, so the
misses come one after another instead of overlapping, which is why B's cancel is the slowest op.
A follows fewer pointers (bucket, then node) but does a malloc and a free per order, and a 16 byte
order becomes a ~48 byte node. C has one random access per add or cancel (a slot in a 16 MB
table), and the rest of its data (80 KB of qty, 1.2 KB of bitmap) stays in L1/L2.

### 11.4 Page faults

C causes the most page faults (25,153 vs 19,610 for A and 18,787 for B), even though it uses
the least memory per order (~32 B vs 48 B or more in A and B).

1. They only happen at the start. `--runs 1` and `--runs 5` give the same count (C: 25,152 vs
   25,154), because after the first couple of books glibc reuses memory it already has. About 8k of each count is the 32 MB op list.
2. C's last two tables (8 and 16 MB) are big enough that glibc gives each one its own mmap and
   hands it back to the kernel when it's freed, so the second book faults them in again. A's and
   B's nodes are small mallocs that stay in malloc's free lists. With
   `MALLOC_MMAP_THRESHOLD_=67108864 MALLOC_TRIM_THRESHOLD_=1073741824` (glibc keeps big blocks
   and doesn't hand freed memory back), C drops to 16,161 faults,
   close to A (15,372) and B (15,943) (`results/page_faults.txt`).

Giving C a 32 MB table from the start (`-DORDER_TABLE_INIT_CAP=2097152`) made it worse:
100,959 faults instead of 25,155, and 44.9 ms instead of 33.4 ms
(`results/experiment_presize.txt`, a separate back to back run). glibc only raises its mmap limit
up to 32 MB (it's in `man mallopt`), so each of the 11 books the bench creates mmaps and zeroes a
fresh 32 MB table. That happens in the constructor, before the timer starts, so the faults aren't
what made the loop slower. We didn't profile that build, but our guess is the size: every add and
cancel now hits a random slot in 32 MB for the whole run, while the growing table is 8 MB or less
for the first half of the run, so there are more cache and dTLB misses.

### 11.5 Production choice

We would pick C, with the table allocated once at startup and sized for the expected peak, not
bigger (see 11.4).

- Latency: lowest on every op, and its cancel is about 5x faster than A's. Cancels and
  modifies are most of the messages in a real market.
- Predictability: every op does a small, fixed amount of work (one probe, one array write, at
  worst a 79 word bitmap scan), while A and B depend on tree depth, cache state and malloc. C's one
  spike is the resize (the last doubling re-inserts 262k orders), which pre-sizing removes.
- Memory: a fixed 80 KB for the levels and a 16 byte slot per order (~32 B at half load), with no
  allocator overhead.
- Cache behavior: about 5x fewer L1 misses than A per run, and the hot data fits in L1/L2.
- Complexity: this is C's weak point. It has a hand written hash table (the backward shift delete
  is easy to mess up) and bit tricks, while A is ~100 lines of standard containers.
- Maintainability: OK because `--verify` checks C against a brute force book after every op, but C
  relies on the price band being small and fixed. A works for any price.

We wouldn't pick B: it's the hardest to reason about (stale entries, `in_heap`) and has the slowest
cancel and depth queries. If latency didn't matter we'd pick A, because it's the simplest.

### 11.6 What would we try next?

- Take integer ticks in the API. `llround` alone is 2.0% of C's samples, and
  tick to double is most of `get_bbo`.
- Allocate the order table once and pre-touch it, maybe with huge pages (16 MB is 4096 normal
  pages but only 8 huge ones, so fewer dTLB misses).
- Prefetch the hash slot as soon as we know the id.
- A pool of orders with a FIFO list per level, which matching (price-time priority) would need anyway.
- Measure p99 and max latency, not just averages, which hide things like C's resize.
