# Performance Report

- **Name:** Prateek Singh
- **Email:** prateek.24bcs10135@sst.scaler.com
- **Roll no:** 24BCS10135

All numbers below come from the same workload: 1,000,000 operations, fixed seed
42, roughly 70% add / 20% cancel / 10% BBO, with `get_bids(10)` and `get_asks(10)`
run every 100 operations. Each benchmark is 5 runs and I report the median.

Machine: ASUS VivoBook (Intel 12th-gen hybrid CPU), Ubuntu, g++ with `-O2`. The
CPU has P-cores and E-cores, so perf shows two PMUs. The process ran almost
entirely on the P-cores, so I use the `cpu_core` counters (97 to 100% coverage)
for everything below. `cpu_atom` had under 3% coverage and huge error bars, and
`cpu_atom/L1-dcache-load-misses` was reported as `<not supported>`, so I ignore
the atom column. Raw captures are in `results/`.

One measurement note: the benchmark wraps every single operation in
`steady_clock::now()` so it can split time by operation type. That adds a fixed
per-call overhead of a few tens of nanoseconds, which inflates the very cheap
operations (BBO especially) by the same amount for all three books. The
comparison between A, B and C stays fair, but the absolute ns for BBO should be
read as "small and dominated by timing overhead".

## 10. Results Table

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 306.93 | 310.94 | **165.69** |
| ns / add | 262.55 | 194.15 | **150.49** |
| ns / cancel | 415.15 | 622.57 | **105.96** |
| ns / BBO | 36.68 | 54.26 | **33.56** |
| IPC | 1.05 | **1.37** | 1.36 |
| Branch misses (%) | 4.90% | **2.06%** | 2.38% |
| L1-dcache-load-misses | 65,592,292 | 50,956,054 | **22,899,284** |
| Cache misses (LLC) | **10,549,821** | 13,359,106 | 16,506,965 |
| Page faults | **18,007** | 18,838 | 66,199 |

(IPC, branch-miss %, cache and page-fault rows are the `cpu_core` counters from
`perf stat -r 5`. Bold marks the best value in each row.)

Final BBO and checksum agreed for all three: `Bid 99.99 x 2722`, `Ask 100.01 x
2525`, checksum `8622340677121175029`. The `--verify` run confirms A, B and C
produce identical BBO and depth snapshots after every one of the 1,000,000
operations.

## 11. Performance Analysis

### 11.1 Which implementation is fastest?

C is fastest on every single operation.

- **add:** C 150 ns, B 194 ns, A 262 ns. C just indexes into a flat array and
  adds to a counter, then stores the order in its own hash table. A pays for a
  red-black tree insert on the price side (O(log P)) on top of the order hash.
  B's add is cheaper than A's because a heap push is amortised O(1) and it only
  pushes a tick when a level first becomes active, but it still costs more than a
  raw array write.
- **cancel:** C 106 ns, A 415 ns, B 622 ns. This is the biggest gap. C does one
  hash lookup and one array decrement. A has to find the price node in the tree
  and possibly erase it. B is the slowest because its order records live in a
  `std::map` (a tree), so every cancel is an O(log N) tree walk with N up to a few
  hundred thousand live orders, and that is a lot of pointer chasing.
- **BBO:** C 33 ns, A 37 ns, B 54 ns, all small. C and A read a cached position
  or `begin()` directly. B is slower because it may have to pop stale ticks off
  the heap top and do a hash lookup to read the quantity.

So C wins mainly because its hot paths are plain array indexing with no tree or
heap work and no pointer chasing.

### 11.2 Branch Prediction

A has the most branch misses by rate: 4.90%, versus 2.06% for B and 2.38% for C.

A spends most of its time in `std::map`, which is a red-black tree. Every lookup
and insert walks the tree making a less-than comparison at each node, and which
way it goes next depends entirely on the random price, so the CPU cannot predict
the direction. On top of that, inserts and erases trigger rebalancing, which is
more unpredictable branching. B actually executes the most branches in total
(1.11 billion) but predicts them better, because heap sift-up/sift-down and hash
probing follow more regular patterns. C has few branches overall (0.46 billion)
and they are mostly predictable array-scan and first-probe-hit branches, so both
its branch count and miss rate stay low.

### 11.3 Cache Behavior

This one is interesting because the two cache counters point in different
directions.

- **L1 data cache misses:** A is worst at 65.6M, B 51.0M, C best at 22.9M. A and
  B are node-based: tree nodes, hash buckets and heap entries are scattered
  across the heap and reached by following pointers, so the L1 is constantly
  missing. C keeps price levels in two contiguous arrays and orders in one flat
  table, so a lot of accesses hit lines that are already warm.
- **Last-level (LLC) cache misses:** here C is actually worst at 16.5M, A best at
  10.5M. C's backing memory is large (two 5,000-entry level arrays plus a 64K-slot
  order table that grows), and the random price stream touches it sparsely, so a
  fair number of accesses fall all the way through to DRAM.

The point is that C does far less total memory work and keeps the common case in
L1, which is what actually drives latency, so it still wins overall even though
its LLC miss count is the highest. It trades a few more deep misses for a lot
fewer shallow ones.

### 11.4 Page Faults

C causes by far the most page faults: 66,199 against about 18,000 for A and B.

That is a direct result of C allocating big blocks up front. The two level arrays
and the order table are large contiguous allocations, and the open-addressed
table also doubles and rehashes as it fills, so the kernel has to fault in a lot
of fresh pages the first time each is touched. A and B grow one small node at a
time and reuse freed nodes, so they touch fewer brand-new pages. These are
one-time minor faults that happen while the book warms up, not a steady-state
cost, but they do tell me C's memory footprint is bigger and would benefit from
pre-faulting.

### 11.5 Production Choice

I would deploy **C**, but not only because it has the lowest average latency.

- **Latency:** C is fastest on add, cancel and BBO, and cancel in particular is
  3 to 6 times quicker than the others, which matters because cancels are common
  in real books.
- **Predictability:** this is the real reason. C has no tree rebalancing and no
  heap that can grow without bound, so it has far fewer surprises in the tail. A
  can rebalance at awkward moments and B's lazy heap and tree-based order store
  make its worst case the shakiest of the three.
- **Memory usage:** C uses a fixed, known amount for the price arrays, which is
  easy to reason about and cap. Its one weak spot is the cold-start page faults,
  which I would remove by pre-touching the arrays and reserving the order table at
  start-up.
- **Cache behavior:** the contiguous layout keeps the hot path in L1.
- **Complexity and maintainability:** this is where C loses points. It ships its
  own open-addressed hash table with tombstones, which is more code to own and
  test than just using the standard library. The `--verify` and `--test` runs
  give me confidence it is correct, but a team has to maintain it.

If maintainability mattered more than latency (for example a non-critical
service), A is the sensible fallback: it is short, uses only the standard library,
and is easy to read. For a latency-critical trading path, C's speed and
predictability are worth the extra code.

### 11.6 What Would You Try Next?

- **Pre-fault and reserve** C's arrays and order table at start-up to remove the
  66K cold-start page faults.
- **Denser types:** store quantities as 32-bit where the domain allows, to pack
  more levels per cache line and cut the LLC misses from 11.3.
- **Better order table:** try Robin Hood hashing or a small open-addressing load
  factor to shorten probe chains further.
- **Cleaner benchmark timing:** time each operation type in its own pass instead
  of wrapping every call, so the BBO numbers are not dominated by `clock()`
  overhead.
- **Bound B's weak spots:** move B's order records from `std::map` to a hash map
  (that alone should fix most of its slow cancel) and compact its heaps when they
  collect too many stale entries.
- **Branchless best-price repair** in C, and aligning the arrays to cache-line
  and page boundaries.
- **Profile at higher resolution:** record with a hardware cycles event and DWARF
  call graphs for sharper symbol attribution than the short default run gave here.
