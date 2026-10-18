# Performance Report: Low-Latency Order Book

Parth Sankhla, 24BCS10229, parth.24bcs10229@sst.scaler.com

## 0. Setup

| | |
| --- | --- |
| CPU | AMD Ryzen 7 7840HS (8 cores / 16 threads), L1d 32 KB per core, L2 1 MB per core, L3 16 MB |
| OS | CachyOS, Linux 7.2.9 |
| Compiler | g++ 16.2.1, `-std=c++17 -O2 -g -fno-omit-frame-pointer` |
| perf | 7.2.9, `perf_event_paranoid = 2` so only user space is counted (events show `:u`) |

Laptop plugged in. All three benchmarks were run back to back so they are comparable. The CPU clock still moves a bit between runs, so for comparing the designs I lean on `cycles` and `instructions` too, since those don't depend on clock speed (they stayed within about 1-2% between runs). Raw outputs are in `results/`.

## 1. Complexity

L = live price levels on a side (at most 5000), N = live orders, k = orders resting at one price, H = heap size including stale prices, D = depth asked for, K = array slots walked.

| Operation | A (map of vectors) | B (heap + map) | C (array) |
| --- | --- | --- | --- |
| `add_order` | O(log L) map + O(1) avg hash + O(1) amortized `push_back` | O(log N) order map + O(1) avg hash, + O(log H) push only for a new level | O(1) |
| `cancel_order` | O(1) avg hash + O(log L) map + O(k) search and erase in the level | O(log N) order map + O(1) avg hash, + O(log H) pops when the top level empties | O(1), + O(K) walk when the best level empties |
| `get_bbo` | O(1) | O(1) (+2 hash lookups for the qty) | O(1) |
| `get_bids` / `get_asks` | O(D) | O(H) heap copy + O(D log H) pops | O(K), about D when the book is dense |

In this workload the book fills up fast (about 500k live orders over 10,000 prices, so k is around 50 orders per price), and levels almost never go empty. That makes C's walk and B's stale pops rare, but A's O(k) search on cancel happens every time.

## 2. Results

1,000,000 ops, seed 42, median of 5 runs (`results/bench_*.txt`). Counters are from `perf stat -r 5` (`results/perf_stat_*.txt`) and cover the whole program: building the workload once plus the 5 timed runs.

| Metric | A (Map) | B (Heap) | C (Array) |
| --- | --- | --- | --- |
| Total time (ms) | 286.40 | 217.61 | **56.34** |
| ns / add | 227.40 | 107.04 | **27.26** |
| ns / cancel | 480.19 | 475.36 | **62.04** |
| ns / BBO | **21.92** | 33.02 | 26.56 |
| cycles | 7.29 B | 5.95 B | **1.32 B** |
| instructions | 4.02 B | 5.62 B | **1.75 B** |
| IPC | 0.55 | 0.94 | **1.33** |
| Branch misses (%) | 5.29% | 1.94% | **1.61%** |
| L1-dcache-load-misses | 129.8 M | 166.1 M | **13.8 M** |
| Cache misses | 57.8 M | 36.3 M | **1.7 M** |
| dTLB-load-misses | 7.65 M | 4.66 M | **11.7 K** |
| Page faults | 17,163 | 11,678 | **4,440** |

All the counters I asked for were available. None were missing.

About the ns numbers: each op is timed on its own with two `steady_clock::now()` calls, and that pair by itself measured about 19 ns on this machine. So roughly 19 ns of every number in the table is the timer. The real BBO cost for A and C is only a few ns (that's why they look tied), and C's real add and cancel costs are smaller than shown. `perf record` agrees: for C, over half the samples are in `clock_gettime`. The ordering between the three doesn't change.

## 3. Analysis

### 3.1 Which one is fastest?

C is fastest for add (about 4x B, 8x A) and cancel (about 8x both A and B). For BBO all three are just reading a value they already have, so A and C are tied once you take the timer off. B is slower at BBO because after `top()` it still does a hash lookup on each side to get the qty.

C wins because every operation is a direct index: `orders[id]` and `level[price]`. There's no hashing, no tree walk and no `malloc`. It runs 2-3x fewer instructions than A and B, and each instruction is cheaper because the data is already in cache (IPC 1.33 against 0.55 and 0.94).

A is now the slowest overall. It's the only one that keeps every order inside its price level (the `map<price, vector<Order>>` idea, for price time priority), and that costs:

- On add it walks the price map, inserts into the id hash map, and pushes onto the level's vector, which sometimes has to grow and copy the whole vector.
- On cancel it has to search the level's vector for the id (about 50 orders here) and then `erase` from the middle, which shifts every order behind it. `perf report` shows `MapBook::cancel_order` as the biggest function at 24%.

B doesn't store orders per level, only a total qty, so its add is about 2x faster than A's. Its cancel is about the same as A's but for a different reason: it looks up a random id in a 500k node red-black tree (about 19 levels of pointer chasing) and rebalances on erase (`_Rb_tree_rebalance_for_erase` is 7% of B). B also copies the whole heap for every `get_bids` / `get_asks`.

### 3.2 Branch prediction

A has the most branch misses by far (5.3% of branches, 43.7 M). Most of it comes from walking `std::map` keyed on price on every add and cancel. At each node the CPU has to guess left or right, and with random prices that's a coin flip at every level of a tree about 12 deep. The loop that searches a level's vector for the id adds more: it exits at a random position each time, so the loop exit is hard to predict.

B has fewer misses (1.9%) because its tree is keyed by order id, and on add the id is always bigger than everything already in the tree, so "go right" is always the right guess. Cancels on B still walk the tree with a random id, and the heap's sift up / down is data-dependent.

C has the fewest (1.6%). Its branches are things like "is this price better than the best" or "did the level become empty", which are nearly always false, so they predict well.

### 3.3 Cache behaviour

A has the most cache misses (57.8 M) and dTLB misses (7.65 M). B has the most L1 misses (166 M). C is far lower on all of them (1.7 M cache misses, 30x fewer than A).

- A follows pointers to reach a level (map nodes are separate allocations), then reads through a vector of 32-byte orders to find the id, and the erase moves the rest of that vector. With 10,000 levels each holding its own vector, all of that is spread over a lot of memory, so most of it isn't in cache.
- B follows pointers too: every `std::map` node and every `unordered_map` node is its own small allocation. It does two of these lookups per op (order tree and qty hash map), which is where its L1 misses come from. It also copies the heap vector for every depth query: contiguous, but thousands of ints copied to read 10.
- C only touches contiguous memory. The level array is 15,001 longs (about 120 KB), so the busy part around the best prices stays in L1/L2. `orders[id]` is a single array access. Cancel ids are random so it can still miss there, but it's one miss per cancel, not a chain of them.

### 3.4 Page faults

A has the most page faults (17,163), then B (11,678), then C (4,440). Part of every run is the same (the 1M op workload vector is 32 MB), so the difference between them is the book. A allocates the most: one hash map node per order, one map node per level, and 10,000 vectors that keep reallocating as they grow (the old block is freed and a bigger one taken), so it keeps touching fresh pages. B allocates one tree node and sometimes a hash node per order. C allocates its arrays once up front (`reserve` for the orders, a fixed level array), so after that it reuses the same memory and barely faults.

### 3.5 Production choice

I'd pick C.

- Latency: lowest for add and cancel, tied for BBO.
- Predictability: this matters more than the average. A and B call `malloc` / `free` on the hot path, rebalance trees or grow vectors, so some ops are randomly slow. C does no allocation in steady state. Its one slow case is the walk when the best level empties, and that is bounded (at most 5000 slots, all contiguous). If that ever became a problem a bitmap of non-empty levels would fix it.
- Memory: fixed and small for levels (120 KB). The order array grows with the largest id, which is fine for an exchange-style counter id, but it would need a hash map (or id remapping) if ids were random 64-bit values. That's the main weakness of C.
- Cache behaviour: by far the best, as above.
- Complexity / maintainability: C is the simplest of the three to read (two arrays and two ints). But C doesn't keep orders in time order inside a level, it only has totals. That's all this assignment needs since there is no matching. A is the only one that keeps price time priority, so if matching were added A's layout is the one that already supports it. I'd keep A as the reference in tests, the way `--verify` already uses it. B is the most fiddly (lazy deletion, duplicate prices in the heap) and slow at cancel, so I wouldn't ship it.

C only works because the price band is small and fixed (10,000 ticks). For an instrument with an unbounded price range it would need an array around the current mid price that re-centres itself.

### 3.6 What I'd try next

- Take the per-op timer out of the hot loop (time batches, or use `rdtsc`) so the ns numbers aren't mostly timer overhead.
- For A, store the position of each order in its level so cancel doesn't search, or use a linked list per level so erase doesn't shift. Also `reserve` the level vectors so they don't keep reallocating.
- Add price time priority to C with a per-level queue of order ids, if matching were needed.
- Keep a bitmap of non-empty levels in C so finding the next best price is a couple of word scans instead of walking slots.
- Use an object pool for A and B to take `malloc` out of the hot path and see how close they get.
- Give B a `bool live[price]` array instead of the qty hash map, and stop copying the heap for depth queries.
- Pin the benchmark to one core and use the performance governor, so wall time is as stable as the cycle counts.
