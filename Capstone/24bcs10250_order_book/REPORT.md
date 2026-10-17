# Performance Report

All numbers are from a friend's Ubuntu laptop (Intel i7), with the program pinned to one core. Times are the median of 5 runs of `./bench`. Counters are from `perf stat -r 5`, and that output is in `results/`.

| Metric | A map | B heap | C array |
|---|---|---|---|
| Total time (ms) | 280.1 | 114.2 | 53.5 |
| ns / add | 221.7 | 73.6 | 31.6 |
| ns / cancel | 502.6 | 129.9 | 42.8 |
| ns / BBO | 15.7 | 29.6 | 15.6 |
| Instructions | 5.33 G | 4.20 G | 2.14 G |
| IPC | 0.80 | 1.32 | 1.92 |
| Branch misses (%) | 4.59% | 1.08% | 1.40% |
| Branch misses (count) | 56.8 M | 8.2 M | 5.1 M |
| L1-dcache-load-misses | 87.9 M | 107.3 M | 6.3 M |
| Cache misses | 18.6 M | 8.7 M | 2.4 M |
| dTLB-load-misses | 11.7 M | 3.5 M | 0.30 M |
| Page faults | 21,825 | 20,728 | 24,219 |

Every op is timed with two `steady_clock::now()` calls, and reading the clock costs time too. In C's `perf report` the clock code is about a third of all samples. A and C both show about 15.6 ns for BBO even though it is only a couple of loads, so most of that is the clock.

## 11.1 Which implementation is fastest?

C is fastest at add (31.6 ns) and cancel (42.8 ns), and its total time is about 5x lower than A. For BBO, A and C are the same because both just read a stored value (`begin()` of the map, or the saved best index). B is slower at BBO (29.6 ns) because it has to look up the heap top in the hash map to check it is still a live level, and sometimes pop dead prices.

C wins because add and cancel are just an array index and an add. There is no searching and no `malloc`. It runs 2.14 G instructions against A's 5.33 G and gets 1.92 instructions per cycle against A's 0.80.

A is slowest because every add and cancel goes through two red-black trees. The orders tree has about 500k live orders, so it is around 19 levels deep, and each level is a separate node somewhere in memory. Cancel is the worst (502 ns) because it looks up an old random id, so most of that path is not in cache, then erases the node with rebalancing and a `free`. In `perf report`, A spends 37% in `add_order` and 26% in `cancel_order`, plus tree rebalancing and `malloc`.

B is in the middle. Hash lookups are O(1), but each one still jumps to a node in memory, and rehashing takes 5% when the orders map grows.

## 11.2 Branch prediction

A has the most branch misses: 56.8 M, 4.59% of all branches. A tree search is a chain of left or right decisions on random ids and prices, and the CPU can't guess those. Rebalancing adds more branches that depend on the data.

B and C have far fewer (8.2 M and 5.1 M). C's percentage is a little higher than B's only because C runs far fewer branches overall (363 M vs 760 M). B's hash table code has lots of easy, predictable branches that lower its percentage. Some misses are the same for all three, like the benchmark loop choosing between add, cancel and BBO for a random op. Inside C the branches are mostly predictable. For example `if (i > best_bid)` is almost always false once the book is full.

## 11.3 Cache behaviour

A has the most last-level cache misses (18.6 M) and dTLB misses (11.7 M). It follows pointers and allocates many small objects. Every `std::map` node is its own allocation of about 48 to 64 bytes, and there are about half a million of them. They don't fit in cache and are spread over many pages, so each step of a search is likely a cache miss and often a TLB miss too.

B has the most L1 misses (107 M). `perf report` shows 9% of B's time in `memmove`, which is `get_bids`/`get_asks` copying the heap. Each heap is up to 5000 prices × 8 bytes = 40 KB, so the two together are bigger than the L1 cache. That copy also allocates a new 40 KB buffer every time, which is probably why malloc's internal functions show up (about 7%). The copy reads memory in order, so these misses are cheaper than A's random ones. B's last-level and dTLB misses are much lower than A's.

C has the fewest misses on every cache counter (6.3 M L1, 0.3 M dTLB). The level arrays are 40 KB per side and contiguous. New orders go to the end of the orders vector, and a cancel is one load at `orders[id]` instead of a chain of pointers.

## 11.4 Page faults

C has the most page faults (24,219), which surprised me, so I ran 1 run and 5 runs separately (`results/page_faults.txt`):

| | 1 run | 5 runs |
|---|---|---|
| A | 21,825 | 21,824 |
| B | 19,326 | 20,730 |
| C | 16,015 | 24,216 |

For a single run C has the fewest. Building the 1M op workload is part of every column and is the same for all three. The difference is what happens between runs:
- A and B use lots of small `malloc` blocks. After a run frees them, the memory stays in the process and the next run reuses it, so later runs add almost no faults.
- C reserves its orders vector as one 24 MB block. A block that big comes straight from the OS and goes back to it when freed, so every new run starts with fresh pages and faults again (about 2,000 more per run).

So it is about allocation behaviour, not C using more memory. In a real system the book is created once, so C would pay this only at startup, while A and B call `malloc` and `free` on every add and cancel.

## 11.5 Production choice

I would deploy C.

- Latency: lowest for add, cancel and BBO.
- Predictability: I only measured averages, but from the code every op does a small fixed amount of work with no `malloc`, no rehash and no rebalancing. The one slow case is cancelling the last order at the best price, which scans to the next non-empty slot. That scan is at most 5000 contiguous slots.
- Memory: levels are a fixed 80 KB. Orders are 24 bytes per id with no node overhead, but the vector keeps a slot for every id ever seen, including cancelled ones.
- Cache: fewest L1, cache and dTLB misses.
- Complexity: a bit more code than A but simpler than B. B's lazy delete was the one place I needed the verify test to trust it.

The catch is the orders vector indexed by id. It only works because ids here are small and increasing. With real exchange ids I would keep the arrays for price levels and put orders in a hash map sized up front. A is the easiest to write, but a 500 ns cancel is too slow.

## 11.6 What I would try next

- C spends 10% resizing the orders vector, because `add_order` calls `orders.resize(id + 1)` for every new id. Part of that is touching new memory for the first time, but growing in big chunks would remove the extra call on every add.
- Keep a bitmap of non-empty levels in C so finding the next best level doesn't need a scan.
- Use 32-bit price and qty so `Order` is 16 bytes instead of 24 and more orders fit in a cache line.
- In B, stop copying the heap for depth queries, since that copy is 9% of B's time.
- In A, use a pool allocator for map nodes instead of `malloc` for each one, and keep orders in a hash map since only price levels need to be sorted.
- Time batches of ops instead of single calls, or subtract the clock cost, so BBO shows its real cost.
