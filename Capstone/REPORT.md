# Performance Analysis Report: Low-Latency Order Book

## 0. Test Environment

| | |
| :--- | :--- |
| CPU | 13th Gen Intel Core i5-13500H (hybrid: 4 performance cores + 8 efficiency cores, 16 threads) |
| Caches | L1d 48 KB per P-core, L2 9 MB total, L3 18 MB shared |
| OS / Kernel | Pop!_OS, Linux 7.1.5 |
| Compiler | g++ 13.3.0, `-std=c++17 -O2 -g -fno-omit-frame-pointer` |
| perf | 7.1.5, `perf_event_paranoid = 2` (user-space counting only, events carry the `:u` suffix) |

Raw outputs are in `results/` (`bench_*.txt`, `perf_stat_*.txt`, `perf_report_*.txt`, `machine.txt`).

---

## 1. Complexity Comparison (Big-O)

| Operation | A (Map) | B (Heap) | C (Fixed Array) |
| :--- | :--- | :--- | :--- |
| **`add_order`** | O(log L) map lookup/insert + O(1) avg hash + O(1) list append | O(log N) order-map insert + O(1) avg level hash + O(log H) heap push (only for a new level) | O(1) avg (hash insert + direct array index) |
| **`cancel_order`** | O(1) avg hash + O(log L) map find + O(1) list erase | O(log N) order-map find/erase + O(1) avg level hash + amortized O(log H) stale pops | O(1) avg hash + O(K) scan when the best level empties |
| **`get_bbo`** | O(1) (`begin()`) | O(1) (`top()` + one hash lookup) | O(1) (array read at best index) |
| **`get_bids(D)` / `get_asks(D)`** | O(D) iterator walk | O(H) heap copy + O(D' log H) pops | O(K) scan until D non-empty levels are found |

L = live price levels per side (at most 5000), N = live orders, H = heap size including stale ticks, D = requested depth, D' = D plus skipped stale/duplicate ticks, K = array slots scanned (at most 5000; about D when the book is dense, as in this workload).

---

## 2. Benchmark Results

Workload: 1,000,000 operations (about 70% add, 20% cancel, 10% BBO; `get_bids(10)` + `get_asks(10)` every 100 operations), `std::mt19937` seed 42. Latencies are timed with `std::chrono::steady_clock`, median of 5 runs. Counters are from `perf stat -r 5`, `cpu_core` (performance-core) rows; the process ran 97–99% of the time on P-cores, so the `cpu_atom` rows are small multiplexed samples and are not used.

| Metric | A (Map) | B (Heap) | C (Fixed Array) |
| :--- | :--- | :--- | :--- |
| **Total time (ms)** | 251.57 | 209.45 | **99.46** |
| **ns / add** | 218.28 | 107.77 | **58.23** |
| **ns / cancel** | 386.01 | 480.75 | **187.72** |
| **ns / BBO** | 16.91 | 32.09 | **16.83** |
| **IPC** | 0.69 | 1.25 | **1.43** |
| **Branch misses (%)** | 4.06% | 1.86% | **1.20%** |
| **L1-dcache-load-misses** | 98.4 M | 100.9 M | **22.5 M** |
| **Cache misses** | 104.7 M | 23.0 M | **15.1 M** |
| **dTLB-load-misses** | 12.5 M | 5.3 M | **3.1 M** |
| **Page faults** | 24,325 | **17,971** | 18,293 |

Unavailable counter: `L1-dcache-load-misses` is `<not supported>` on the efficiency cores (`cpu_atom`); it is available on the performance cores used above.

**Measurement caveats**
- `perf stat` measures the whole process: workload generation (identical for all three) and destroying the book after each of the 5 internal runs. The timed loop excludes these. This is why A's task-clock (2.40 s) is much larger than 5 × 252 ms: freeing hundreds of thousands of list, map and hash nodes is expensive but untimed.
- Each `get_bbo` timing wraps a 1–2 ns operation in two `steady_clock::now()` calls (about 15 ns together), so A's and C's ~17 ns are mostly timer overhead and cannot be distinguished. B's extra ~15 ns is real.
- A's perf runs varied by ±6.9%, versus under 2.2% for B and C.

---

## 3. Performance Analysis

### 3.1 Which implementation is fastest?
C is fastest for every operation: **add 58 ns** (vs 108 ns B, 218 ns A), **cancel 188 ns** (vs 386 ns A, 481 ns B), and **BBO ~17 ns** (tied with A, B is 32 ns).

`perf record` shows where the time goes (share of P-core cycles):
- **A:** `cancel_order` 19.0%, `add_order` 16.5%, `std::map::operator[]` 11.2% plus 9.2% in `_Rb_tree::_M_erase` (4.6% per side), which is the map destructor freeing nodes between runs and is not part of the timed loop, and **about 23% inside the allocator** (`unlink_chunk` 6.8%, `malloc_consolidate` 6.6%, `_int_free` 3.2%, `_int_malloc` 2.9%, `malloc` 2.1%, `cfree` 1.3%). Every add allocates a list node, a hash node and sometimes a map node, so A is dominated by tree walking and small allocations.
- **B:** `cancel_order` 28.8%, almost all of it inside **`std::map::find` on the order ID** (tree descent), plus 3.6% in `_Rb_tree_rebalance_for_erase`. `add_order` is 18.1%, and the level hash maps 9.5%. The heap copy in `top_levels` costs only about 5% (1.2% + 3.5% `memmove`). B's slow cancel comes from its tree-based order index, not from the heap.
- **C:** `cancel_order` 27.3%, the **order-ID hash map about 19%** (several `_Hashtable` entries), and the **allocator about 19%** (one `unordered_map` node per order). The price-level array itself barely appears. The `[vdso]` clock reads are 12.2% of C's cycles, because its operations are so cheap that timing them becomes a large share.

C's add is fast because finding the level is one subtraction (`price - MIN_BID_TICK`) instead of a tree or hash lookup. B's add beats A's because a level hash lookup and an occasional `push` onto a contiguous `vector<int>` are cheaper than A's tree descent plus list-node allocation.

### 3.2 Branch Prediction
**A has the most branch misses (4.06%, 43.1 M)**, more than twice B (1.86%) and over three times C (1.20%). Every `std::map` lookup walks about 12 tree levels for ~5000 levels, and at each node the left/right decision depends on random price data, which the predictor cannot learn. Inserts also run red-black rebalancing, with more data-dependent branches.

B's remaining misses come from its order-ID `std::map` walks (the same problem as A, but only one tree per operation instead of two structures) and from heap sift-up/sift-down comparisons. C executes the fewest branches overall (0.64 B vs 1.06 B for A) because the level lookup is arithmetic. Its remaining misses are mostly in hash-map probing and in the "did the best level empty?" scan.

### 3.3 Cache Behavior
**A has by far the most cache misses: 104.7 M last-level misses**, 4.5× B and 7× C, and the highest dTLB misses (12.5 M). Every operation follows pointers across separately allocated heap nodes (a hash node, then a list node, plus map nodes for the level), scattered across memory. That pointer chasing is why A's IPC is only 0.69: the CPU mostly waits for memory.

B has as many L1 misses as A (100.9 M) but far fewer last-level misses (23.0 M). Its per-order data is a single tree node, and its per-level data (two hash maps of at most 5000 entries plus two small `vector<int>` heaps) stays in L2/L3.

C has the fewest misses at every level (22.5 M L1, 15.1 M LLC). The level array is contiguous, so neighbouring prices share cache lines, and the whole array (10,000 × 16 B = 160 KB) fits in L2. It is larger than the 48 KB L1, so it does not live entirely in L1. C's remaining misses come from the order hash map.

### 3.4 Page Faults
**A causes the most page faults (24,325).** B (17,971) and C (18,293) are nearly equal. About 8,000 faults are a common baseline: the pre-generated workload vector is about 1.02 M × 32 B ≈ 32 MB, which is touched once. A's ~6,000 extra faults are about 24 MB of additional heap memory for its per-order list nodes and the extra hash and map nodes.

C is not lower than B because both still allocate one node per order: a `std::map` node in B, an `unordered_map` node in C. C's fixed level array is only about 40 pages, so it barely affects the total. A "pre-allocated" design therefore only helps page faults when the per-order storage is pre-allocated too.

### 3.5 Production Choice
I would deploy **C (Fixed Array)**.
- **Latency:** lowest for add (58 ns) and cancel (188 ns), and tied for BBO.
- **Predictability:** no tree rebalancing and no heap copies. Its main remaining risks are the allocator calls in the order hash map (about 19% of its cycles) and the linear scan when the best level empties. That scan is short in a dense book but can reach 5000 slots in a sparse one, which creates a latency tail.
- **Memory:** a fixed 160 KB level array plus one hash node per live order. That is less than A, which uses three structures per order.
- **Cache behaviour:** the fewest L1, LLC and dTLB misses, and the highest IPC (1.43).
- **Complexity and maintainability:** the simplest code of the three, although it relies on the fixed, bounded price band (`[50.00, 150.00]`, 0.01 tick). For an instrument with an unbounded or very wide price range, A's design would be the general fallback.

### 3.6 What Would You Try Next?
1. **Replace the order-ID `unordered_map`** in C with a pre-allocated flat array or an open-addressing hash table. This targets the ~19% hash-map time and the ~19% allocator time measured in C.
2. **Object pool or arena for orders** in A and B, to remove `malloc`/`free` from the hot path (about 23% of A's cycles).
3. **Bitmap of non-empty levels** (one bit per tick, using `__builtin_clzll` / `__builtin_ctzll`) to find the next best level in a few instructions instead of a linear scan.
4. **Cache-line alignment:** `alignas(64)` on C's array, laid out so that the best-bid and best-ask slots (indices 4999 and 5000) fall in the same 64-byte line. At 16 B per level and a 64-byte-aligned base, they currently land in adjacent lines.
5. **For B:** replace the `std::map` order index with a hash map (the profile shows it is B's main cost), and walk ticks down from `heap.top()` for depth queries instead of copying the heap.
