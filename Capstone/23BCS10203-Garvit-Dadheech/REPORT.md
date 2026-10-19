# Report: Low-Latency Order Book

Garvit Dadheech, 23BCS10203, garvit.23bcs10203@ms.sst.scaler.com

The designs and how to build/run are in README.md. Everything below comes from
the files in results/ which I generated on my own laptop.

## Setup

- Laptop: MacBook Air, Apple M2 (4 P cores + 4 E cores, 128 KB L1D, 16 MB L2
  shared), 16 GB RAM.
- Timings: run natively on macOS, Apple clang 17, -O2 -std=c++17,
  std::chrono::steady_clock.
- perf: Ubuntu 24.04 docker container on the same laptop (kernel 7.0
  linuxkit, g++ 13.3 -O2 -std=c++17, perf 6.8).
- Workload: 1,000,000 ops with mt19937(42): 700,829 adds, 199,496 cancels,
  99,675 BBO queries, plus 10,000 x (get_bids(10) + get_asks(10)).
- Because adds are 70% and cancels 20% the book only grows, so by the end
  there are about 500k live orders spread over all 10,000 levels.

The per-op numbers include the two steady_clock::now() calls around each op.
On this machine that pair costs about 27 ns (the benchmark prints it, and the
`--impl none` run, where the book does nothing, shows ~25 ns per op). So for
the real cost of a call subtract roughly 26 ns.

## Correctness output (results/verify.txt)

```
Running correctness test...

Workload: 1000000 ops, seed 42
Checked : 1010000 BBO snapshots, 199496 cancel results, 10000 depth snapshots (top-10 and full book), plus edge cases

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

All three benchmark runs also print the same final BBO (99.99 x 3244 /
100.01 x 2193) and the same checksum 17464855786638258893 on both macOS and
Linux. I also ran a separate stress test (8M ops, 40 seeds, 30-80% cancels,
price bands as narrow as 3 ticks, and a tiny starting table for C so it
rehashes a lot) and it found no mismatches either.

## Results table

Timings are native macOS, median of 5 runs (results/bench_*.txt). Counters
are from perf stat -r 5 inside the Linux container (results/perf_stat_*.txt).

| Metric | A (MapBook) | B (HeapBook) | C (ArrayBook) | none (loop only) |
|---|---:|---:|---:|---:|
| Total time (ms) | 260.50 | 263.98 | 73.14 | 52.85 |
| ns / add | 219.7 | 77.4 | 43.5 | 25.2 |
| ns / cancel | 359.6 | 227.3 | 43.2 | 25.8 |
| ns / BBO | 27.5 | 31.2 | 28.5 | 25.3 |
| ns / get_bids(10)+get_asks(10) | 174.9 | 12503.5 | 104.3 | 25.3 |
| task-clock (ms), perf stat, includes workload generation | 458.9 | 175.0 | 122.7 | 75.8 |
| IPC | not supported in VM (1) | | | |
| Branch misses (%) | not supported in VM (1) | | | |
| L1-dcache-load-misses | not supported in VM (1) | | | |
| Cache misses | not supported in VM (1) | | | |
| Page faults | 22998 | 16773 | 13828 | 9712 |
| Page faults minus the `none` baseline | 13286 | 7061 | 4116 | |

(1) The docker VM on Apple Silicon does not expose the PMU, so perf stat
prints `<not supported>` for cycles, instructions, branches, branch-misses,
cache-misses, L1-dcache-load-misses and dTLB-load-misses. task-clock and
page-faults are software counters so they work. The section "Counters from
the M2" below has the hardware counters read directly on the Mac.

Taking the ~26 ns timer cost out, the actual cost per call is about:
A add 194 ns, cancel 334 ns; B add 51 ns, cancel 201 ns; C add 17 ns,
cancel 17 ns; get_bbo is 2-5 ns for all three.

The same code on Linux/glibc (results/bench_*_linux.txt) ranks the same way
but the gaps are bigger: A 762 ms (add 727 ns, cancel 678 ns), B 236 ms
(add 130, cancel 348), C 149 ms (add 117, cancel 115), none 70 ms. glibc's
malloc/free is slower than macOS's for this pattern, and the clock call in the
VM costs ~32 ns instead of 27.

## perf stat output (Linux container, --reps 1 --no-timers)

Implementation A

```
 Performance counter stats for './build-linux/bench --impl A --reps 1 --no-timers' (5 runs):

            458.91 msec task-clock                       #    0.986 CPUs utilized               ( +-  6.12% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             22998      page-faults                      #   50.114 K/sec                       ( +-  0.00% )

            0.4653 +- 0.0287 seconds time elapsed  ( +-  6.18% )
```

Implementation B

```
 Performance counter stats for './build-linux/bench --impl B --reps 1 --no-timers' (5 runs):

            174.96 msec task-clock                       #    0.982 CPUs utilized               ( +-  3.05% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             16773      page-faults                      #   95.869 K/sec                       ( +-  0.00% )

           0.17809 +- 0.00540 seconds time elapsed  ( +-  3.03% )
```

Implementation C

```
 Performance counter stats for './build-linux/bench --impl C --reps 1 --no-timers' (5 runs):

            122.66 msec task-clock                       #    0.990 CPUs utilized               ( +- 17.90% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             13828      page-faults                      #  112.737 K/sec                       ( +-  0.00% )

            0.1239 +- 0.0220 seconds time elapsed  ( +- 17.78% )
```

Baseline (--impl none)

```
             75.84 msec task-clock                       #    0.977 CPUs utilized               ( +-  5.24% )
              9712      page-faults                      #  128.067 K/sec
           0.07761 +- 0.00411 seconds time elapsed  ( +-  5.29% )
```

## Counters from the M2 (sudo ./scripts/run_apple_pmu.sh)

tools/apple_pmu.cpp sets up the M2 counters through kperf, runs the same loop
as bench with --no-timers, and reads the thread's counters right before and
after the loop, so workload generation is not included. Apple has no last
level cache miss event, so that row can't be filled on this machine. I report
L1D load/store misses and L1/L2 dTLB misses instead.

| Metric (median of 5, operation loop only) | A | B | C |
|---|---:|---:|---:|
| cycles | TBD | TBD | TBD |
| instructions | TBD | TBD | TBD |
| IPC | TBD | TBD | TBD |
| branches | TBD | TBD | TBD |
| branch-misses (%) | TBD | TBD | TBD |
| L1-dcache-load-misses | TBD | TBD | TBD |
| L1-dcache-store-misses | TBD | TBD | TBD |
| dTLB-load-misses (L1 dTLB) | TBD | TBD | TBD |
| L2 dTLB misses | TBD | TBD | TBD |
| page-faults | TBD | TBD | TBD |

Raw output: results/apple_pmu_{A,B,C}.txt

## perf record / perf report (Linux container, --reps 3 --no-timers)

Top self-time entries from results/perf_report_flat_*.txt (--no-children).
This is the profiling build where the five public methods are noinline so
they show up by name. Percentages are of the whole process, and about 10% of
each profile is workload generation (make_workload, mersenne_twister_engine).

A (MapBook), 47K samples

```
25.46%  MapBook::add_order
16.94%  MapBook::cancel_order
13.10%  libc  _int_free
 9.35%  libc  malloc_consolidate
 6.17%  libc  cfree
 4.71%  std::_Hashtable<..., MapBook::Handle>::_M_rehash
 3.29%  libc  _int_malloc
 2.82%  libc  unlink_chunk
 1.81%  libc  malloc
 1.36%  std::_Hashtable<..., MapBook::Handle>::_M_emplace
 0.73% + 0.72%  std::_Rb_tree<int, PriceLevel>::_M_erase / _M_emplace_hint_unique
```

Around 36% of A's time is in glibc malloc/free, since list nodes, hash nodes
and tree nodes are all separate allocations. The 42% inside add_order and
cancel_order is mostly the inlined red-black tree walk and the list/hash chain
pointer chasing.

B (HeapBook)

```
14.89%  libc  malloc
 9.82%  libc  __memcpy_generic           (copying the heap in get_bids/get_asks)
 7.82%  libc  _int_free
 7.30%  libc  malloc_consolidate
 6.70%  HeapBook::add_order
 6.53%  libc  _int_malloc
 3.42%  libc  cfree
 3.29%  std::_Hashtable<..., HeapBook::Order>::_M_rehash
 3.24% + 2.97%  std::__adjust_heap (bid heap, ask heap)
 2.89%  HeapBook::cancel_order
 2.65%  std::_Hashtable<int,long>::try_emplace
 2.06%  libc  unlink_chunk
```

About 42% allocator, ~10% memcpy from copying the heap 20,000 times for the
depth queries, ~6% heap sift-down.

C (ArrayBook)

```
32.51%  ArrayBook::add_order
15.52%  ArrayBook::cancel_order
11.26%  ob::make_workload                (harness)
 9.66%  ob::run_once<ArrayBook>          (harness: loop + checksum)
 5.05%  mersenne_twister_engine          (harness)
 3.45%  __llround + llround@plt          (double price -> tick)
 2.20%  [kernel] el0_da                  (page faults, first touch of the table)
 1.65%  std::vector<OrderSlot>::_M_fill_assign   (zeroing the 16 MiB table once)
 0.94% + 0.75% + 0.55%  get_bids / get_asks / get_bbo
```

No malloc anywhere. A quarter of the samples are the harness itself. The only
thing worth pointing out is llround: converting the double price into a tick
is 3.5% of the process, more than get_bbo, get_bids and get_asks together.

## Analysis

### 11.1 Which implementation is fastest?

C is fastest for all three operations.

| | add | cancel | get_bbo |
|---|---|---|---|
| fastest | C (~17 ns real) | C (~17 ns real) | C and A about the same (~2 ns), B ~5 ns |
| C vs A | 5x | 20x | same |
| C vs B | 3x | 12x | same |

For the depth query C is also the fastest (104 ns), A is 175 ns and B is
12.5 us, 120x slower.

Why C is faster: an add is hash the id (5 integer ops), probe one 16 byte
slot, add into an 80 KB array that stays in cache, set one bit, compare with
the cached best. A cancel is the same in reverse. No malloc, no free, no
pointer chasing, no tree walk. So the only slow thing per op is one cache miss
on the big order table.

A does a tree walk for every add and cancel. With 10,000 levels the red-black
tree is ~14 deep and prices are uniformly random, so almost every walk goes
down to a leaf, that is ~13 dependent pointer loads. Then an add mallocs a
list node and a hash node. A cancel does a hash lookup into a cold node, the
same tree walk again, a list unlink and two frees. That is why cancel in A
(334 ns) is slower than add (194 ns): free has to touch the header of a cold
chunk, and malloc_consolidate alone is 9% of the profile.

B gets rid of the tree but keeps the allocator. Add is reasonable (51 ns)
because the hash insert goes to the head of a bucket and the node comes from
malloc which is usually warm. Cancel (201 ns) has to find a random old node
(cold bucket, cold node) and free it. The depth query is terrible because
std::priority_queue can only give you elements in order by popping, so
get_bids copies the whole heap (a few thousand ints, that is the 10% memcpy)
and then pops ten times.

get_bbo is O(1) in all three so the differences are within timer noise. A
reads the leftmost tree node (always hot), B reads the heap top and does one
hash lookup for the qty, C reads two ints and two longs.

### 11.2 Branch prediction

A should have the most branch misses, then B, and C the fewest.

A: every level of the tree walk is a data dependent branch (go left or right)
and with random prices each one is close to a coin flip, so ~6 mispredicts per
walk, and the walk happens on every add and cancel. Rebalancing when a level
is inserted or erased is a lot of case branches (this happens a lot early in
the run, rarely in steady state). The hash chain and list loops have
unpredictable trip counts. Inside glibc, malloc/free fast path vs
consolidation depends on the history of the heap and mispredicts too, and A
spends 36% of its time in there.

B: the hash probe loop (while node && key != key) has a data dependent trip
count, __adjust_heap is a loop of data dependent compares every time a level
is created and a tick is pushed, and the prune loop branches on whether the
top is stale. B is 42% malloc/free with the same problems as A.

C: the probe loop almost always runs 0 or 1 extra iteration (load is under
0.5 for most of the run) so its exit branch is predicted well. The was_empty
branch in add is basically never taken once all 10,000 levels are populated.
The "did I just empty the best level" branch in cancel is rarely taken. The
bitmap search uses clz/ctz instead of a loop. The only real 50/50 branch is
the side check, which the compiler can turn into a csel.

### 11.3 Cache behaviour

A has the most cache misses, B next, C the fewest.

A follows pointers everywhere and allocates lots of small objects. Tree nodes
(~64 B each, 10,000 of them, ~640 KB), list nodes (32 B, one per live order,
~500k so ~16 MB) and hash nodes (another ~16 MB plus a ~4 MB bucket array) are
all malloc'd one at a time, so neighbours in a list or a chain are wherever
malloc put them. Cancelling a random old order misses on the bucket, the hash
node, the lower levels of the tree path, the list node and both its neighbours
(unlink writes prev/next of two more cold nodes). That is something like 5-7
cache misses per cancel, which matches the ~334 ns (3-4 memory round trips).

B jumps around memory too but less: one hash node per order (~500k x 32-48 B
spread over ~20 MB) plus the bucket array. An add hits a random bucket (miss)
and writes a fresh node (usually warm from the allocator); a cancel hits a
random bucket and a random old node (two misses) and frees it. The level maps
(5,000 entries each) and the heaps (~20-40 KB) stay in L2.

C uses contiguous fixed memory. The price array is 80 KB and the bitmap 1.3
KB, both always hot. The order table is 16 MiB of 16 byte slots, so a random
id lands on a random line: one expected cache miss per add and per cancel, and
the probe stays on the same line (4 slots per 64 B line) or the next one.
Nothing is allocated on the hot path so there is no allocator metadata to
miss on either.

This is the main reason C is 5-20x faster than A. The instruction counts are
not that different, A just stalls on memory several times per op and C once.

### 11.4 Page faults

A has the most (22,998, or 13,286 above the baseline), then B (7,061 above
baseline), then C (4,116 above baseline).

A mallocs three kinds of small objects per order, so the heap grows page by
page as the book fills and every new page faults on first touch. 13,286 x 4
KiB is about 52 MiB which matches ~500k live orders x (32 B list node + 32 B
hash node) plus the bucket array plus fragmentation from the 200k frees.

B allocates one node per order plus the bucket arrays, which it grows by
doubling (_M_rehash is 3% of its profile), so each doubling touches a new,
bigger array and the faults come in bursts.

C has a fixed footprint: 80 KB of levels + 1.3 KB of bitmap + one 16 MiB order
table (2^20 slots x 16 B = 4,096 pages, which is exactly the ~4.1k faults
measured). They all happen on first touch and never again. With MAP_POPULATE
or huge pages they would disappear from the hot path completely. The kernel
entries in C's profile (el0_da, clear_page) are these first touches.

So the page fault counts say: A and B grow memory bit by bit through a general
purpose allocator, C reserves what it needs up front and never asks the OS
for anything after that.

### 11.5 Production choice

I would ship C, with two changes.

Latency: it is the best on every op, by a lot on add and cancel.

Predictability: this is more important than the average. A and B call
malloc/free on every add and cancel, so their tail latency depends on what the
allocator decides to do (malloc_consolidate alone is 7-9% of their time and
runs in bursts). B also has the 12 us depth query. C's hot path has no
allocation, no loop of unbounded length except the probe run, and O(1) best
price recovery through the bitmap, so its latency distribution is tight. The
one spike C can have is the rehash when the table doubles, which is an O(n)
stall. In production I would size the table for the maximum number of live
orders (or rehash incrementally) so it never happens while trading. That is
change one.

Memory: C is bounded and known in advance (16 MiB + 82 KB here). A and B grow
with live orders plus allocator overhead and fragmentation, and already touch
2-3x more pages.

Cache: one miss per op versus several, and the price level side of C lives in
L1/L2 permanently.

Complexity and maintainability: this is where C is the worst of the three.
Backward shift deletion and the two level bitmap are ~80 lines of code with
invariants that are easy to break, which is why the verify mode compares full
depth after every snapshot and why the stress test uses a tiny table. It also
hardcodes that the price universe is a fixed 10,000 tick range; if the band or
tick size changes that is a constant and the memory size changes with it. A is
the one a new person understands in five minutes, and for anything that is not
latency critical I would use A. For the trading path the extra ~150 lines are
worth it as long as the tests stay. Change two is to keep those tests in CI.

B is the one I would not ship: it has the allocator cost of A plus a depth
query that is two orders of magnitude slower, and lazy deletion means its
memory grows with stale heap entries in a way that is hard to reason about.

### 11.6 What I would try next

- Integer ticks in the API. llround(price * 100) is 3.5% of C's whole process,
  more than all of its query functions together. The feed handler already has
  the price as an integer, so taking an int32 tick removes the multiply, the
  round and the convert on every add and in every returned level.
- Pre-fault or huge pages for the order table. madvise(MADV_HUGEPAGE) or
  MAP_POPULATE turns the 4,096 first touch faults (and the TLB pressure of
  4,096 4 KiB pages vs 8 x 2 MiB pages) into nothing on the hot path.
- Direct indexed order table if ids are dense. Exchange ids are usually
  sequential, so a vector indexed by id - base makes cancel a single load with
  no hash and no probe, at the cost of memory for gaps.
- Split the slot into two arrays. Cancel only needs {tick, qty}; keeping the 8
  byte id separately halves the bytes on the probed line and lets 8 keys share
  one cache line.
- Prefetch. If ops arrive in a queue, __builtin_prefetch on the slot of the
  next cancel while handling the current one hides the one remaining miss.
- Fewer branches: compute the side check as a select, and use the bitmap for
  get_bbo too so there is no has_bid branch.
- For A and B, a pool allocator. A fixed size free list for list/hash nodes
  would remove the ~40% of time under malloc/free and most of their page
  faults, and would make nodes contiguous so cache misses drop too. A std::map
  with a node pool is a perfectly usable production book.
- For B, replace the priority_queue with the bitmap. The lazy deletion heap
  only exists to find the best price; a 10,000 bit map does that in O(1) with
  no stale entries and makes get_bids O(d) instead of O(H).
- Measure percentiles, not averages. Put every op's latency in a histogram and
  compare p99 / p99.9. That is where the allocator based designs lose, and it
  would make the predictability argument above a number instead of a claim.
