# Low-Latency Order Book - report

Kushal Talati (24BCS10123, kushal.24bcs10123@sst.scaler.com), Arjun Aggarwal (24BCS10109, arjun.24bcs10109@sst.scaler.com)

## 1. Environment

One laptop (Apple M3 Pro MacBook), two places:

- **linux, docker** (perf + timings): Ubuntu 24.04.5 container, Linux 6.12.54-linuxkit aarch64, g++ 13.3.0, perf 6.8.12, 6 vcpus. macOS has no `perf`, so sections 8 to 10 of the assignment were done here
- **mac, native** (timings only): macOS 26.5.1, apple clang 21. with homebrew gcc's libstdc++ the `steady_clock` only ticks every microsecond on macOS, with libc++ every ~42 ns, so we used apple clang for the mac numbers

Flags: `g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic`, zero warnings. For perf we add `-g -fno-omit-frame-pointer`, debug info only. perf commands, exactly as in the assignment:

```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X --runs 1 --notime
perf record -g ./bench --impl X --runs 1 --notime ; perf report --stdio --no-children | head -80
```

**Unavailable counters:** inside the Docker VM cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses and dTLB-load-misses all came back `<not supported>` (raw output in `perf/stat_*.txt`). Only task-clock and page-faults worked. `perf_event_paranoid` was 2 and the container ran privileged, the VM just does not expose a PMU. So we ran the same perf-build binary under valgrind cachegrind in the same container (`valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes --I1=131072,8,64 --D1=131072,8,64 --LL=16777216,16,64 ./bench --impl X --runs 1 --notime`, valgrind 3.22). That gives instructions, branches, branch misses, L1d misses and LL misses for the whole process, exact and repeatable, but from a simulator and not the real CPU: the cache geometry is the M3 Pro p-core (128 KB L1, 16 MB shared L2 as the last level, 64 B lines, L1i rounded down from 192 KB because cachegrind wants powers of two) and the branch predictor is cachegrind's simple one, so the miss % is an upper-ish bound. Raw output in `perf/cachegrind_X.txt`, per function in `perf/cachegrind_X_funcs.txt`.

`--notime` skips the per op `steady_clock::now()` pairs for the perf runs, otherwise `__kernel_clock_gettime` was 68% of C's profile. perf stat covers the whole process (workload generation, untimed warm-up, one run), the same for all three. Timed runs are the median of 5 in-process runs on a fresh book after one untimed warm-up. The clock overhead (one `now()` pair in an empty loop) is 21 ns and is included in every ns/op, not subtracted.

## 2. Design

All three share `order_book_api.hpp`. Prices are integer ticks inside (`tick = llround(price * 100)`, so 99.98 is 9998), bids 5000..9999, asks 10001..15000, 5000 levels a side. The API still takes and returns `double`. No matching, orders rest until cancelled, level qty is the sum of its orders. `add_order` returns false (and does nothing) for an out of band price, qty <= 0 or a reused id. We reject instead of asserting so a bad message can't take the book down.

### A: `OrderBookMap`

| | |
|---|---|
| level storage | `std::map<int32_t, long>` per side, bids with `std::greater` so `begin()` is the best |
| order storage | `std::unordered_map<uint64_t, OrderRec>` |
| best lookup | `begin()` of the map |
| cancel lookup | hash find on id, then `map::find` on the tick, subtract, erase the level if it hits 0 |

| op | cost | why |
|---|---|---|
| add | O(log L) + O(1) avg | map insert/find on at most L=5000 levels, one hash insert |
| cancel | O(log L) + O(1) avg | hash find, map find, maybe erase |
| get_bbo | O(1) | `begin()` on both maps, the tree keeps the leftmost node cached |
| get_bids/asks(k) | O(k) | walk k nodes from `begin()`, each step is a pointer chase |

### B: `OrderBookHeap`

| | |
|---|---|
| level storage | `std::unordered_map<int32_t, long>` per side for qty, plus a `std::priority_queue<int32_t>` of candidate best ticks (max heap for bids, min heap for asks) |
| order storage | `std::unordered_map<uint64_t, OrderRec>` |
| best lookup | `top()` of the heap, after popping stale tops |
| cancel lookup | hash find on id, then subtract from the level hash. the heap is not touched |

Lazy deletion: cancel only subtracts qty and never touches the heap, so the heap can hold ticks whose level is now 0. Invariant: a tick is in the heap iff it is a key in the level map. add pushes only when the key is missing, so a dead level still sitting in the heap is not pushed twice. `get_bbo` pops tops while their qty is 0 and erases the key, so a later add re-pushes it. Popping a stale top is safe because a 0 qty level is not the best by definition and heap order depends only on the tick. Every push is matched by at most one pop, so the pops are amortised over the adds.

| op | cost | why |
|---|---|---|
| add | O(log L) worst, O(1) avg when the level exists | hash find on level, heap push only on an empty level |
| cancel | O(1) avg | two hash lookups, no heap work |
| get_bbo | O(1) amortised, O(S log L) worst | S = number of stale tops sitting above the live best. each stale entry is popped once |
| get_bids/asks(k) | O(L + k log L) | copies the heap (L entries) then pops k live ones, skipping stale |

### C: `OrderBookArray`

| | |
|---|---|
| level storage | `std::array<long, 5000>` per side, indexed directly by tick. index 0 is always the best price on both sides (bids are stored reversed, idx = 9999 - tick, asks idx = tick - 10001) |
| order storage | `std::vector<OrderRec>` slab indexed by order id, with a `live` flag. ids are dense (1, 2, 3, ...) in this workload. ids beyond the slab go to a small `unordered_map` so correctness never depends on the assumption |
| best lookup | cached `best` index per side, plus a 79 word occupancy bitmap. when the best level empties we scan the bitmap from the next bit with `__builtin_ctzll` |
| cancel lookup | `slab[id]` directly, `live` tells us if it was unknown / already cancelled |

Memory: 80 KB of level qty (2 x 5000 x 8 B), 1.3 KB of bitmap, and a 12 MB slab (2^20 x 12 B). Levels and bitmap sit in L2, the slab does not but every access to it is one direct index, at most one miss and no pointer to follow. Everything is sized in the constructor, the hot path never allocates.

| op | cost | why |
|---|---|---|
| add | O(1) | one slab store, one array add, one bit set, one compare for best |
| cancel | O(1) usual, O(79) worst | slab load, array subtract. if the best level just emptied, scan up to 79 bitmap words (bounded, no branch per bit) |
| get_bbo | O(1) | read the cached best index and the array |
| get_bids/asks(k) | O(k + 79) | k ctz steps along the bitmap, each `find_from` is O(words skipped) |

### how they differ

| | A map | B heap | C array |
|---|---|---|---|
| order storage | unordered_map | unordered_map | vector slab by id (+ overflow map) |
| level storage | std::map (rb tree) | unordered_map qty + priority_queue of ticks | fixed std::array by tick |
| best lookup | map begin() | heap top after lazy pops | cached index + bitmap ctz |
| cancel lookup | hash + tree find | hash + hash | direct index, live flag |

## 3. Results

1,000,000 ops, seed 42: 700,471 adds, 199,303 cancels, 100,226 get_bbo, plus get_bids(10)+get_asks(10) every 100 ops, 0 degraded cancels. Table built by `scripts/make_table.py` from `results/` and `perf/`, the instruction, branch and cache rows pasted from `perf/cachegrind_X.txt` (L1-dcache-load-misses = cachegrind D1mr, loads only like the perf event, cache misses = LL misses, reads + writes).

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 288.6 | 196.6 | 83.9 |
| ns / add | 204.5 | 109.0 | 40.6 |
| ns / cancel | 562.7 | 370.4 | 113.5 |
| ns / BBO | 42.7 | 43.8 | 46.1 |
| Instructions | 1,383,142,808 | 1,470,388,546 | 532,333,542 |
| Branches | 200,390,688 | 182,510,357 | 49,878,144 |
| Branch misses | 18,057,619 | 5,280,482 | 2,582,745 |
| Cycles | 3,104,932,500 | 1,577,191,500 | 315,414,000 |
| IPC | 0.45 | 0.93 | 1.69 |
| Branch misses (%) | 9.0 | 2.9 | 5.2 |
| L1-dcache-load-misses | 18,891,616 | 15,185,537 | 2,211,739 |
| Cache misses | 4,446,345 | 4,392,404 | 1,670,976 |
| dTLB-load-misses | 4,000,000 | 2,000,000 | 200,000 |
| Page faults | 8,156 | 7,925 | 2,227 |


| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 232.8 | 152.0 | 54.4 |
| ns / add | 159.5 | 75.4 | 24.2 |
| ns / cancel | 461.8 | 264.5 | 53.9 |
| ns / BBO | 22.9 | 37.0 | 20.6 |

Both platforms rank the three the same way. On top of the required rows: depth (bids+asks, 10 levels) is A 196 / B 1,602 / C 164 ns on linux and 150 / 2,119 / 98 on the mac, and the linux p99 is add 583 / 333 / 84 ns, cancel 1,666 / 1,417 / 542 ns. perf stat task-clock for the whole process: A 767 ms (+-18%), B 389 ms (+-8%), C 78 ms (+-5%). The VM is noisy, the mac a bit less so, about +-15% between repeated sets of 5 for A.

The clock ticks every 41.67 ns on both platforms (24 MHz counter), so single call times are multiples of ~42 ns. That is why ns/BBO is 20-46 ns everywhere, that is the 21 ns overhead plus one tick, and why p99 values are things like 83, 334, 417.

## 4. Analysis

### 11.1 Which implementation is fastest?

- **add: C** (41 ns, B 109, A 204 on linux, mac 24 / 75 / 160). C does one store into `slab[id]` (sequential, ids go up by one), one add into the level array, one bit set and one compare. No lookup. B does a hash find on the level map and a hash insert for the order, the heap push only happens when a level was empty, which is rare once ~500k orders sit on 10,000 levels. A pays the hash insert plus a red-black tree find/insert with rebalancing and a node allocation for each new level.
- **cancel: C** (113 ns, B 370, A 563, mac 54 / 264 / 462). C is one load from `slab[id]`, a subtract and a bit clear, the bitmap scan only when the best level dies. It is still ~3x slower than C's add because cancels hit a random old id in a 12 MB slab, so that load is a cache miss, while adds write the next slot and the prefetcher hides it. A is slowest: hash find, hash erase (a free), tree find and sometimes a tree erase (another free). B skips the tree and never touches the heap on cancel.
- **get_bbo: a tie.** All three keep the best price at a known place (`map::begin()`, heap `top()`, a cached index) and the measured 20-46 ns is mostly the clock. We expected C to win clearly and it did not, there is nothing left to win.
- depth, not in the table but measured: C 164 ns, A 196, B 1,602. B copies the whole heap and pops 10 levels twice per call, the O(L + k log L) from its table. Our first C version did not `reserve()` the output vector and lost to A here, the fix was one line.

### 11.2 Branch prediction

Measured with cachegrind (section 1): A 18.1M mispredicts, 9.0% of 200M branches. B 5.3M, 2.9% of 183M. C 2.6M, 5.2% of 50M. So A has by far the most, as expected, and the surprise is C's percentage being higher than B's. The per function table explains it: about 2.25M mispredicts are the same in all three and are not the book at all, `gen_workload` 0.83M (random op type and side), the op `switch` in the run loop 0.71M, and the `side == Buy` branch of `tick_in_band` inlined into `add_order` 0.70M (the side is a coin flip, no predictor can learn it). Subtract that floor and the book itself mispredicts about 15.8M times in A, 3.0M in B and 0.33M in C, i.e. 17.6, 3.4 and 0.37 per add/cancel. C just executes so few branches (50M vs 200M) that the harness floor dominates its percentage.

Where they come from, from `cachegrind_X_funcs.txt`. A: `stl_tree.h` alone is 13.0M of the 18.1M, the `std::map` find/insert/erase and rebalancing inside `add_order` and `cancel_order` (`cancel_order` is 2.9M on its own), then `malloc`/`malloc_consolidate` 1.2M and the hash table 0.3M. B: `malloc.c` 1.3M, `__adjust_heap` 0.84M (the sift on push and on every stale pop), the hash tables 0.3M, `drop_stale` barely shows. C: `cancel_order` 0.2M (the `qty == 0` / `idx == best` checks and the bitmap loop when the best dies) and `add_order` 0.02M beyond the side branch. The code reading we had before matches: A should have the most: every step of `map::find` / insert is a data dependent compare on a random tick, about 12 steps deep for 5000 nodes, plus the rebalancing branches on insert and erase, plus the `unordered_map` bucket walk. B keeps the hash branches but drops the tree, and the `drop_stale` loop in get_bbo almost never iterates. C's only data dependent branches are `qty[idx] == 0` (rare, ~50 orders per level), `idx == best` (almost always false) and the `while (word == 0)` loop in `find_from`, which runs only when the best dies and usually finds the next bit in the same word. The cancel p99s (A 1,666, B 1,417, C 542 ns) point the same way, we just can't put a percentage on it.

### 11.3 Cache behaviour

Measured: L1d load misses A 18.9M, B 15.2M, C 2.2M, last level misses A 4.4M, B 4.4M, C 1.7M (cachegrind, 128 KB L1d / 16 MB LL). Again there is a shared floor: streaming the 24 MB workload vector costs ~0.75M L1d misses in every build (0.40M in the run loop + 0.35M in the inlined `to_price`), and 1.5M of the LL misses in A and B are the first touch of ~0.5M freshly malloc'ed nodes (`new_allocator.h` / `malloc.c`, writes that miss). So the book's own L1d misses are roughly A 18M, B 14M, C 1.3M. A: 10.0M of them are in `stl_tree.h` (the tree walk, each node a separate line) and 3.3M in `malloc.c` (`malloc_consolidate` and `unlink_chunk` walking free lists), the order hash table adds 1.5M. B: the two `unordered_map`s are 4.8M + 2.5M (`hashtable.h` + `hashtable_policy.h`, bucket then node, two dependent loads), `malloc.c` 3.3M, and `memcpy` 0.95M read + 0.83M write misses which is the heap copy in `get_bids`/`get_asks`, 20k copies of a ~5000 int heap. C: `add_order` 0.71M (the level array and bitmap compete with the streaming workload for L1), `cancel_order` 0.56M (the random `slab[id]` load, one per cancel, 0.2M of them go all the way to LL), nothing else. Instruction counts tell the same story: A 1.38G, B 1.47G, C 0.53G, and a third of A and B is `malloc.c` (454M / 473M instructions) which C does not execute at all. The code reading from before: A: every level is its own tree node and every order its own hash node, so the tree walk is pointer chasing across the heap and it allocates and frees small objects all the time, `cfree` is 5.6% of its profile. Jumps around memory, follows pointers, many small objects: all three bad answers. B: level lookup is bucket -> node (two dependent loads) plus the same order hash as A, so still pointer chasing but a shorter chain, and the heap itself is contiguous. C: the 80 KB level array and 1.3 KB bitmap are contiguous and stay in L1/L2 for the whole run. The 12 MB slab is the one thing that misses, sequentially on add and randomly on cancel, which is exactly the 40 vs 114 ns gap. Nothing is allocated in the hot path.

### 11.4 Page faults

Measured: A 8,156, B 7,925, C 2,227 (perf stat, whole process). Everyone pays the 24 MB workload vector, C adds its 12 MB slab touched as ids grow. A and B fault 3.5x more because every order and level is a fresh `new` and malloc keeps asking the kernel for pages as the live set grows to ~500k orders, and `__pi_clear_page` (kernel zeroing new pages) shows in all three profiles. C allocates nothing after the constructor, so once the slab is warm there is nothing left to fault.

### 11.5 Production choice

C, with eyes open. Latency: 5x faster than A on add and cancel. Predictability: p99 add 84 and cancel 542 ns against A's 583 and 1,666, the worst case is a bounded 79 word scan, no allocator in the path so no malloc lock, no page fault, no rebalancing spike. Memory: fixed and known up front (~12 MB slab + 80 KB levels). Cache: two small hot arrays. Complexity and maintainability: ~130 lines of index arithmetic, not harder than A, the bitmap/best repair is the one place a bug would hide. What we would have to fix first: the dense id assumption (real ids are sparse 64 bit, so an id -> slot hash table, roughly one probe, bringing cancel closer to B) and the fixed price band (if the stock leaves 50-150 the book has to re-center, A and B don't care).

A is what we would use when latency does not matter: least code, no assumptions, depth is natural. B we would not ship, get_bbo's worst case depends on how many stale tops have piled up and depth is 10x slower than the others, it only buys cheaper add/cancel over A.

### 11.6 What we would try next

- open addressing id table for C so sparse ids work, and compare it against the slab
- shrink OrderRec from 12 to 8 bytes (tick fits in 16 bits inside the band) so the slab is 8 MB, or back it with hugepages
- intrusive per level FIFO of orders so queue position (price-time priority) can be answered, right now we only keep the sum
- object pool for A and B's nodes, that would be the fair comparison against C
- prefetch `slab[id]` for the next cancel in the stream, it would hide the one miss C has left
- rerun perf stat on a bare metal linux box with a PMU to compare against the containerized counters and cachegrind branch numbers
- TODO: p999 for depth

## 5. Where the time goes (perf report)

From `perf/report_*_flat.txt` (`--no-children -g none`, full call graphs in `perf/report_*.txt`). Sample counts are low because the runs are short, so treat the percentages as rough. `(deleted)` rows are libstdc++, perf lost the library mapping in the container.

- **A** (550 samples): `OrderBookMap::add_order` 34.6%, `cancel_order` 26.7%, `cfree` 5.6%, the order `_Hashtable` 4.9%. Tree insert/erase is inlined into add/cancel, so 61% of the process is the std::map book, plus ~10% hash table and allocator.
- **B** (337 samples): `run<OrderBookHeap>` 17.8% (heap push/pop and `drop_stale` inlined into the loop), `_Hashtable` 10.1%, libstdc++ ~12% (hash node allocation, heap copy for depth), `add_order` 5.3%. B's time is hash tables, not the heap.
- **C** (79 samples): `add_order` 16.5%, `run<OrderBookArray>` 15.2% (get_bbo and the bitmap walk inlined), `cancel_order` 13.9%, `__pi_clear_page` 13.9%, and `gen_workload` + `mt19937` ~20%. When the book is this fast, a fifth of the process is generating the workload and the kernel zeroing pages is as big as cancel.
