# Performance Report

Machine: 13th Gen Intel Core i5-13500H laptop (Ubuntu, g++ 13.3, `-O2`), program pinned to one P-core with `taskset -c 0`. Times are the median of 5 runs of `./bench`. Counters come from `perf stat -r 5` (raw text in `results/`).

| Metric | A map | B heap | C array + bitmap |
|---|---|---|---|
| Total time (ms) | 307.4 | 79.4 | 33.2 |
| ns / add | 252.4 | 86.2 | 89.4 |
| ns / cancel | 787.9 | 208.3 | 81.1 |
| ns / BBO | 15.8 | 33.2 | 14.1 |
| Instructions | 8.04 G | 7.20 G | 3.09 G |
| IPC | 0.51 | 1.31 | 0.99 |
| Branches | 1.83 G | 1.31 G | 0.53 G |
| Branch misses (count) | 115.9 M | 15.2 M | 13.0 M |
| Branch misses (%) | 6.33% | 1.16% | 2.46% |
| L1-dcache-load-misses | 165.5 M | 135.8 M | 24.6 M |
| Cache misses | 72.9 M | 37.2 M | 30.2 M |
| dTLB-load-misses | 45.6 M | 7.0 M | 2.9 M |
| Page faults | 17,914 | 18,752 | 32,987 |

Two things to keep in mind when reading the numbers. (1) Each per-op number includes one empty `steady_clock` pair, about 12-13 ns, and calling the clock also stops the CPU from overlapping neighbouring operations, which is why the per-op numbers add up to more than the total time (C: 33 ms total, but 89 ns x 700k adds alone is 63 ms). So the total time is the better speed measure and the per-op numbers are better for comparing operations. (2) `perf stat` covers the whole process, including building the workload and the timed second pass, so IPC is a whole-program figure.

## 11.1 Which implementation is fastest?

**Total time:** C (33 ms) is about 2.4x faster than B and 9x faster than A.

**add_order:** B and C are tied (86 vs 89 ns, inside the run-to-run spread of C, 82-121 ns); A is about 3x slower at 252 ns. A pays for a node `malloc` plus a tree insert with rebalancing, in two trees (orders and levels). B pays a `malloc` for the hash node and a hash lookup for the level. C pays for no allocation at all, so its add is only slow for one reason: the order-id hash. I multiply the id by a large constant, which sends consecutive ids to random slots of a 24 MB table, so nearly every add misses cache. As a check I temporarily replaced the hash by `id & mask` (sequential ids land in neighbouring slots) and C's add dropped from 89 ns to 28 ns, while cancel stayed at about 80 ns. I kept the scrambling hash in the submitted code because it does not depend on ids being sequential.

**cancel_order:** C is fastest (81 ns), then B (208 ns), then A (788 ns). Cancels pick a random old order, so all three first miss cache on the order lookup. After that A walks a tree of about 19 levels with a miss at most steps, then erases from two trees (rebalancing, `free`). B follows one hash chain and does one `free`. C does one probe, an array subtraction, and possibly a bit clear, and the backward-shift delete moves a few slots in the same cache lines.

**get_bbo:** C (14.1 ns) and A (15.8 ns) are about equal, and most of that is the clock pair itself. A reads `begin()`, C reads two bitmap words and one array slot. B is 2x slower (33 ns) because it must check that the heap top is still live, which is a hash lookup of the level, and sometimes pop dead prices.

## 11.2 Branch prediction

A has the most branch misses by far: 115.9 M, 6.33% of its branches, 7.6x B's count. Every tree search is a chain of left/right decisions on random ids and prices, which the predictor cannot learn, and rebalancing adds more data-dependent branches. B and C are close in count (15.2 M and 13.0 M). C's percentage (2.46%) is higher than B's (1.16%) only because C executes far fewer branches in total (0.53 G vs 1.31 G): the easy, predictable loop branches inside B's hash and allocator code dilute its percentage. Part of the misses in all three comes from the benchmark loop itself, which picks add, cancel or BBO at random for each op, and from the hash-probe loop in C, whose length depends on the data.

## 11.3 Cache behaviour

A has the most cache misses (72.9 M), the most L1 load misses (165.5 M) and by far the most dTLB misses (45.6 M). It follows pointers and allocates many small objects: about 500k live `std::map` nodes of about 56 bytes (plus up to 10k level nodes), each a separate `malloc` block, spread over many pages. Each step down the tree is likely a cache miss and often a TLB miss.

B has fewer last-level and TLB misses (37.2 M, 7.0 M) but still 135.8 M L1 misses. It also allocates one node per order, and every `get_bids/get_asks(10)` call copies a whole heap (up to 5000 prices x 4 bytes = 20 KB per side, plus a new allocation). `perf report` shows `memmove`, `malloc_consolidate`, `unlink_chunk` and hash `_M_rehash` together taking about 20% of B's samples.

C has the fewest on every counter: 24.6 M L1 misses (6.7x fewer than A), 30.2 M cache misses, 2.9 M dTLB misses. Level data is two contiguous 40 KB arrays plus a 640-byte bitmap, which fit in L1/L2, and the order table is one contiguous allocation without pointers. Its remaining misses are the random order-table probes described in 11.1; the order table is also the one thing C holds that does not fit in cache.

## 11.4 Page faults

C has the most (32,987 vs about 18k for A and B), which says nothing bad about its memory use per order (24 bytes per slot at load <= 50%, so about 48 bytes per live order, less than a tree node plus its malloc header). The cause is how it allocates. The order table doubles from 64k slots up to about 1M-2M slots (24-48 MB) and each growth step allocates a fresh block while the old one is still alive; blocks this large come straight from the kernel (`mmap`) and go back to it when freed, so (most likely, I did not trace the allocator) every pass starts again with untouched pages and pays the first-touch faults again. A and B allocate small nodes with `malloc`: memory freed at the end of one pass stays in the process and is reused by the next, so they only fault on their first pass. One run and five runs give almost the same count (A: 17,913 vs 17,915, C: 32,603 vs 32,987), so the extra faults do not come from repeating runs; they are paid inside a single run (its table growth, and the second timed pass). In a real system the book is built once, so C would pay this only at start-up and could avoid it entirely by allocating (and pre-touching) the table up front.

## 11.5 Production choice

I would deploy **C**, but not only because it is fastest.
- **Latency and predictability:** the common operations touch a fixed small amount of memory and never call `malloc`/`free`, so there is no allocator pause and no tree rebalancing. The only unbounded step is the table growth (a rehash of up to 1M slots, a latency spike). That is fixed by sizing the table up front, which is only possible because the maximum number of live orders can be bounded for a real venue. B's `get_bids` copies the heap (O(L)) and its BBO latency depends on how many dead prices are on top, so its tail is much less predictable than its average. A is uniformly slow and its cost grows with the number of orders.
- **Memory:** C's levels cost 80 KB in total and are fixed. Its table is the cost of the order count, about 48 bytes per live order. A and B pay a node plus allocator overhead per order, and B also keeps dead prices in the heap.
- **Cache:** C has 6.7x fewer L1 misses than A and 5.5x fewer than B.
- **Complexity and maintainability:** this is C's weak side. It is the most code (bitmap, hash table with backward-shift delete) and it needs a fixed price band and tick; A is 40 lines of standard containers that are hard to get wrong. I mitigated that with `--verify`, which compares C to A on 1.5M operations including the level-empty cases, and I would keep A as the reference book in tests.

## 11.6 What would I try next?

- Size the order table up front (and `mlock`/pre-touch it) to remove growth spikes and page faults, and try `id & mask` when ids are known to be dense (28 ns add in my test) or a direct `vector<Slot>` indexed by id.
- Remove the 12-13 ns clock-pair overhead: time batches of operations, or read the TSC and calibrate, then re-run `perf stat` with `-r 5` on a per-pass basis.
- Store the order slot as 16 bytes (32-bit qty, 16-bit price tick, 1-bit side) so four slots fit in a cache line instead of 2.7.
- Prefetch the order-table slot a few operations ahead, since the workload is known (`__builtin_prefetch`).
- For B, keep a live-level count and cache the best price so `get_bbo` skips the hash lookup, and avoid copying the heap for depth queries.
- For A, use a pool/arena allocator for the tree nodes, or replace `std::map` with a sorted flat vector for the levels (only 5000 slots per side).
- Pin the process and run `perf stat` with `-r 20` to tighten the confidence intervals; some of A's timings drift upward across runs (241 ms to 346 ms in the five runs) as the heap fragments.
