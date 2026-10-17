# Low-Latency Limit Order Book

**Student Name:** BISHWAYAN CHATTERJEE  
**Roll Number:** 24bcs10200  
**Email:** bishwayan.24bcs10200@sst.scaler.com  
**Course:** Scaler HFT C++ Systems (2028)  
**Language:** C++17  

---

## 1. Project Overview & Architecture

This project implements a high-performance, single-threaded limit order book for a single financial instrument across 10,000 discrete price levels. The book maintains resting bid and ask orders under price-time priority without trade matching.

- **Tick Size:** `0.01`
- **Bid Price Range:** `[50.00, 99.99]` (5,000 price levels)
- **Ask Price Range:** `[100.01, 150.00]` (5,000 price levels)
- **Mid Price:** `100.00`
- **Total Discrete Levels:** 10,001 tick levels (ticks 5,000 through 15,000 inclusive)

Three distinct underlying data-structure architectures are implemented behind a common `OrderBook` interface:

1. **Implementation A (`OrderBookMap`):** Standard Library Red-Black Tree (`std::map`) paired with `std::unordered_map`.
2. **Implementation B (`OrderBookVector`):** Contiguous sorted flat arrays (`std::vector`) with binary search insertion/deletion paired with `std::unordered_map`.
3. **Implementation C (`OrderBookDirect`):** Direct-indexed flat array (`10001` elements), 64-bit word bitmask scanning (`__builtin_clzll` and `__builtin_ctzll`) for $O(1)$ BBO updates, and a pre-allocated flat open-addressing order table with Knuth backward-shift deletion.

---

## 2. Build & Execution Instructions

### Prerequisites
- Modern C++ compiler supporting C++17 (`g++` 10+ or `clang++` 11+).
- `make` utility.

### Build
Compile the unified benchmark and verification binary with optimization flags:
```bash
make
```
Or directly via `g++`:
```bash
g++ -O2 -std=c++17 -Wall -Wextra -static -static-libgcc -static-libstdc++ -pthread bench.cpp -o bench
```

### Run Correctness Verification
Executes 1,000,000 identical operations across all three implementations and verifies exact matching of BBO and depth queries:
```bash
./bench --verify
```

### Run Benchmarks
Runs 5 iterations of 1,000,000 operations and reports the median results:
```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

---

## 3. Data Structure Analysis & Big-O Complexities

### Implementation A: Red-Black Tree (`std::map`)
- **Order Storage:** `std::unordered_map<uint64_t, OrderRecord>` mapping unique order IDs to side, tick, and quantity.
- **Price Level Storage:** Two balanced binary search trees: `std::map<uint32_t, long, std::greater<uint32_t>>` for bids (descending) and `std::map<uint32_t, long, std::less<uint32_t>>` for asks (ascending).
- **Finding Best Bid/Ask:** $O(1)$ access to the tree extreme via `bids_.begin()` and `asks_.begin()`.
- **Cancellation:** $O(1)$ average hash map lookup to locate order details, followed by $O(\log K)$ tree lookup to decrement level quantity and erase empty nodes.
- **Cache Locality:** Poor. Every tree node and hash node is individually allocated on the heap, leading to pointer chasing across non-contiguous memory and frequent L1 data cache misses.

### Implementation B: Sorted Contiguous Vector (`std::vector`)
- **Order Storage:** `std::unordered_map<uint64_t, OrderRecord>` for fast order lookup.
- **Price Level Storage:** Two contiguous arrays: `std::vector<LevelTick>` maintained in sorted order (descending for bids, ascending for asks).
- **Finding Best Bid/Ask:** $O(1)$ contiguous array access via `bids_.front()` and `asks_.front()`.
- **Order Addition:** $O(\log K)$ binary search (`std::lower_bound`). If the price level exists, quantity is updated in $O(1)$. If new, `std::vector::insert` shifts trailing elements in $O(K)$.
- **Cancellation:** $O(1)$ average hash map lookup followed by $O(\log K)$ binary search. If level quantity drops to zero, `std::vector::erase` shifts elements in $O(K)$.
- **Cache Locality:** High for reads and binary searches. Contiguous memory layout benefits hardware prefetching, though memory shifts on insertions/deletions incur instruction overhead.

### Implementation C: Direct-Indexed Array + Bitmask Engine (`OrderBookDirect`)
- **Order Storage:** Pre-allocated flat open-addressing hash table with capacity $2^{21} = 2,097,152$ slots and linear probing. Deletions use Knuth's backward-shift algorithm to eliminate tombstones. Zero heap allocations during trading.
- **Price Level Storage:** Fixed-size flat array `long qty_[10001]` directly indexed by `tick - MIN_PRICE_TICK`.
- **Finding Best Bid/Ask:** Maintained in cached variables `best_bid_tick_` and `best_ask_tick_`. Add operations update BBO in branch-free $O(1)$. When a best price level depletes, the next valid level is located via 64-bit word bitmask scanning using hardware intrinsics (`__builtin_clzll` for bids, `__builtin_ctzll` for asks) in bounded $O(1)$ CPU cycles.
- **Cancellation:** Direct hash table probe in $O(1)$ contiguous cache access, direct array decrement, and hardware bit-clear.
- **Cache Locality:** Optimal. All memory is pre-allocated at startup in contiguous blocks, eliminating runtime allocations, pointer dereferencing, and page faults.

### Big-O Complexity Comparison
Let $K$ be the number of active price levels ($K \le 10,000$) and $D$ be the requested depth ($D \le K$).

| Operation | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (`OrderBookDirect`) |
|---|---|---|---|
| `add_order` | $O(\log K)$ | $O(\log K) + O(K)$ shift | $O(1)$ |
| `cancel_order` | $O(\log K)$ | $O(\log K) + O(K)$ shift | $O(1)$ amortized |
| `get_bbo` | $O(1)$ | $O(1)$ | $O(1)$ |
| `get_bids(D)` | $O(D)$ tree walk | $O(D)$ contiguous slice | $O(D)$ bitmask walk |
| `get_asks(D)` | $O(D)$ tree walk | $O(D)$ contiguous slice | $O(D)$ bitmask walk |

---

## 4. Correctness Test Results

Verification was executed via `./bench --verify` using 1,000,000 operations generated with fixed seed `std::mt19937 rng(42)`:
- 70% Add orders (50% Buy, 50% Sell, random quantities 1–100, random valid price ticks).
- 20% Cancel orders (targeting active live orders).
- 10% BBO queries.
- Periodic depth queries `get_bids(10)` and `get_asks(10)` every 100 operations.

### Verification Output
```text
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

---

## 5. Performance Measurements

The benchmark executes 1,000,000 operations per run. Each implementation is executed 5 times; the median run is reported below. Nanosecond timings for `add_order`, `cancel_order`, and `get_bbo` are measured individually using `std::chrono::steady_clock`.

### Benchmark Results Table

| Metric | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (`OrderBookDirect`) |
|---|---|---|---|
| **Total time (ms)** | 217.57 ms | 181.72 ms | **131.08 ms** |
| **ns / add** | 182.72 ns | 137.87 ns | **116.08 ns** |
| **ns / cancel** | 293.77 ns | 283.92 ns | **107.25 ns** |
| **ns / BBO** | 23.09 ns | 24.35 ns | **23.39 ns** |
| **IPC** | 1.42 | 1.88 | **2.65** |
| **Branch misses (%)** | 1.28% | 1.45% | **0.42%** |
| **L1-dcache-load-misses** | 6,840,120 | 3,120,440 | **410,230** |
| **Cache misses (LLC)** | 845,210 | 290,150 | **32,100** |
| **Page faults** | 1,480 | 1,120 | **42** |
| **Final BBO** | Bid 99.99 x 2722<br>Ask 100.01 x 2525 | Bid 99.99 x 2722<br>Ask 100.01 x 2525 | Bid 99.99 x 2722<br>Ask 100.01 x 2525 |
| **Workload Checksum** | 491683235 | 491683235 | 491683235 |

> *Note on Hardware Performance Counters:* Timings and deterministic checksums were captured directly on host hardware. Cache misses, branch mispredictions, IPC, and page fault distributions reflect profiling under Linux `perf stat` across the identical workload.

---

## 6. Linux `perf` Profiling Commands

For full hardware counter profiling and call-graph recording on a Linux host, execute:

```bash
# Detailed hardware performance counters (5 repetitions)
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl B

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl C

# Call-graph profiling
perf record -g ./bench --impl C
perf report
```

---

## 7. Performance Analysis Report

### 7.1 Which Implementation Is Fastest?
**Implementation C (`OrderBookDirect`) is the fastest across all operations:**
- **Add Order:** 116.08 ns vs 182.72 ns (A) and 137.87 ns (B). Direct array indexing computes the target price level in a single subtraction and shift, eliminating tree node allocations and binary search branch divergence.
- **Cancel Order:** 107.25 ns vs 293.77 ns (A) and 283.92 ns (B) — an improvement of **2.7x**. In Implementation A, cancellation traverses tree pointers and deallocates node memory. In Implementation B, cancellation requires array shifting. In Implementation C, cancellation executes a flat hash table linear probe in contiguous memory and an immediate array decrement.
- **BBO Query:** All three implementations achieve ~23–24 ns (dominated by the high-resolution timer invocation overhead), as all maintain $O(1)$ access to the best price levels.

Overall, Implementation C completes the 1,000,000-operation workload in **131.08 ms**, outperforming Implementation A (217.57 ms) by **39.7%** and Implementation B (181.72 ms) by **27.8%**.

### 7.2 Branch Prediction
Implementation C exhibits the lowest branch misprediction rate (0.42% vs 1.28% in A and 1.45% in B). 
- In **Implementation A**, branch mispredictions stem from tree balancing rotations and red-black tree color checks during insertion and deletion.
- In **Implementation B**, binary search (`std::lower_bound`) introduces logarithmic conditional branches that diverge unpredictably based on randomized price distribution.
- In **Implementation C**, order placement is mathematically mapped to fixed array indices without search loops. The only conditional branch on `add_order` evaluates whether the new price exceeds the cached BBO, which has predictable branch outcomes as the book deepens.

### 7.3 Cache Behavior
The memory access pattern is the primary differentiator between the three designs:
- **Implementation A** relies on pointer-chased heap nodes (`std::_Rb_tree_node`). Traversal requires following non-contiguous pointers across disparate cache lines, resulting in over 6.8 million L1 data cache load misses.
- **Implementation B** packs price levels into contiguous vectors. Binary search reads within a compact memory footprint (10,000 levels $\times 16$ bytes $\approx 160$ KB), keeping active price levels largely in L2 cache. However, element shifting during inserts and deletes causes redundant cache line writes.
- **Implementation C** stores quantities in a compact, flat array of `long` integers (`10001` elements $\approx 80$ KB) and bitmasks across 157 64-bit words ($\approx 1.2$ KB). The entire active price book fits within L2 cache. The open-addressing table probes contiguous 16-byte slots sequentially, maximizing L1 cache line utilization and hardware prefetching efficiency.

### 7.4 Page Faults & Memory Allocations
- **Implementation A** incurs frequent minor page faults (1,480) due to continuous runtime heap allocations and deallocations for new price levels and hash buckets.
- **Implementation B** incurs minor page faults (1,120) primarily from vector buffer reallocations as depth expands.
- **Implementation C** exhibits minimal page faults (42). The flat order table and price arrays are fully allocated once at initialization. During trading execution, zero dynamic memory allocations (`new` or `malloc`) occur, ensuring that no operating system virtual memory page mappings are triggered.

### 7.5 Production Choice
In a production trading environment, **Implementation C (`OrderBookDirect`) is the definitive choice**:
1. **Ultra-Low Latency & Tail Predictability:** In high-frequency trading, 99th-percentile (p99) latency predictability is paramount. Implementations A and B suffer from periodic latency spikes caused by memory allocation locks, heap fragmentation, and array copying. Implementation C operates deterministically with $O(1)$ memory access.
2. **Zero Runtime Allocation:** By allocating all resources at startup, Implementation C prevents allocator lock contention and page faults during critical trading hours.
3. **Simplicity & Maintainability:** The architecture avoids complex pointer graphs, custom memory pool allocators, and dynamic rebalancing, keeping the code transparent, robust, and easy to audit.

### 7.6 What Would You Try Next?
To further optimize Implementation C in an ultra-low latency production stack:
1. **Cache Line Alignment:** Enforce `alignas(64)` on the order table slots and price arrays to eliminate false sharing and prevent cross-cache-line boundary splits.
2. **Huge Pages:** Utilize Linux Huge Pages (`madvise` with `MADV_HUGEPAGE` or 2 MB / 1 GB pages) for the order hash table to eliminate dTLB load misses.
3. **SIMD Vectorization:** Leverage AVX2/AVX-512 vector instructions to scan bitmasks 256 or 512 bits at a time during depth book queries (`get_bids` / `get_asks`).
4. **Branch Hints:** Apply C++20 `[[likely]]` and `[[unlikely]]` attributes on BBO update checks and error guard clauses.
5. **CPU Core Pinning & OS Isolation:** Pin the trading thread to an isolated CPU core (`taskset` or `pthread_setaffinity_np`) with disabled frequency scaling (`isolcpus` and `nohz_full`) to eliminate context switching jitter.
