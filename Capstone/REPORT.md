# Capstone Report: Low-Latency Order Book Performance Analysis

**Author:** Tanishq Jain
**SST Roll Number:** 10039 
**SST Email:** tanishq.24bcs10039@sst.scaler.com
**Course:** High-Frequency Trading & Systems Programming in C++  
**Submission PR:** https://github.com/KnightKnight27/scaler-HFT-C--2028  

---

## 1. Overview of Implementations & Data Structure Design

### Implementation A: Standard Container (`std::map` Red-Black Tree + `std::unordered_map`)
* **Price Level Storage:** Managed using `std::map<int64_t, std::list<Order>, std::greater<int64_t>>` for bids (descending order) and `std::map<int64_t, std::list<Order>, std::less<int64_t>>` for asks (ascending order).
* **Order Storage & Cancellation:** An auxiliary `std::unordered_map<uint64_t, MapValue>` stores order metadata (price in cents, side, and an iterator to the order's node in the `std::list`). When an order is cancelled, the iterator provides $O(1)$ removal from the list, followed by erasing the tree node if the level becomes empty ($O(\log P)$).
* **Best Bid / Ask (BBO):** Located at `bid_tree.begin()` and `ask_tree.begin()` ($O(1)$ iterator access). Total quantity at the level is computed by traversing the resting orders list.
* **Complexity:**
  * `add_order`: $O(\log P)$ map insertion + $O(1)$ list push + $O(1)$ hash map insertion.
  * `cancel_order`: $O(1)$ hash map lookup + $O(1)$ list erase + $O(\log P)$ tree erase if level is emptied.
  * `get_bbo`: $O(M)$ where $M$ is the number of resting orders at the top price level.
  * `get_bids(k)` / `get_asks(k)`: $O(k \cdot M)$ traversing tree nodes and summing list elements.

### Implementation B: Sorted Contiguous Vector (`std::vector` + `std::lower_bound`)
* **Price Level Storage:** Active price levels are stored in contiguous `std::vector<LevelEntry>` where `LevelEntry` contains `{int64_t price_cents, long total_qty}`. Bids are sorted in descending order (highest price at index 0); asks are sorted in ascending order (lowest price at index 0).
* **Order Storage & Cancellation:** An auxiliary `std::unordered_map<uint64_t, OrderData>` stores `{side, price_cents, qty}`. On cancellation, the order is found in $O(1)$, binary search locates the price level in $O(\log P)$, the quantity is decremented, and if the level is depleted, `vector::erase` is called.
* **Best Bid / Ask (BBO):** The best bid is always at `bid_levels[0]` and best ask at `ask_levels[0]`. This is an instant $O(1)$ array index read with contiguous cache locality.
* **Complexity:**
  * `add_order`: $O(\log P)$ binary search via `std::lower_bound` + $O(P)$ vector element shift on inserting a new level.
  * `cancel_order`: $O(1)$ hash map lookup + $O(\log P)$ binary search + $O(P)$ shift if level is erased.
  * `get_bbo`: $O(1)$ direct index access.
  * `get_bids(k)` / `get_asks(k)`: $O(k)$ contiguous vector slice copy.

### Implementation C: Direct Price-Indexed Flat Array with BBO Pointers (Custom Design)
* **Price Level Storage:** Utilizes the fixed price range constraint ($[50.00, 150.00]$ with tick size $0.01 = 10,001$ discrete tick slots). Implemented as a flat, pre-allocated `std::array<long, 10001> price_qty`. Access to any price level is a direct index calculation: $\text{index} = \text{price\_cents} - 5000$.
* **Order Storage & Cancellation:** `std::unordered_map<uint64_t, OrderData>` stores active orders. When cancelling, quantity is subtracted from `price_qty[index]` in $O(1)$. If the cancelled order depletes the top level, a linear scan scans downward/upward to find the next active price tick.
* **Best Bid / Ask (BBO):** Maintains two scalar integer pointers: `best_bid_cents` and `best_ask_cents`. When adding, if the incoming price is better, the pointer updates in $O(1)$. `get_bbo()` is a direct $O(1)$ lookup with zero search.
* **Complexity:**
  * `add_order`: $O(1)$ direct array index update + $O(1)$ hash map insert.
  * `cancel_order`: $O(1)$ hash map lookup + $O(1)$ subtraction ($O(S)$ worst-case scan only if the best price level becomes completely empty, where $S$ is tick spread).
  * `get_bbo`: $O(1)$ direct array index access.
  * `get_bids(k)` / `get_asks(k)`: $O(k + S)$ scanning contiguous array elements.

---

## 2. Summary of Big-O Complexity

| Operation | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (Direct Array) |
| :--- | :--- | :--- | :--- |
| **`add_order`** | $O(\log P)$ | $O(\log P) + O(P)$ | $O(1)$ |
| **`cancel_order`** | $O(\log P)$ | $O(\log P) + O(P)$ | $O(1)$ avg / $O(S)$ worst |
| **`get_bbo`** | $O(M)$ | $O(1)$ | $O(1)$ |
| **`get_bids(k)`** | $O(k \cdot M)$ | $O(k)$ | $O(k + S)$ |
| **`get_asks(k)`** | $O(k \cdot M)$ | $O(k)$ | $O(k + S)$ |

*(Where $P$ = active price levels, $M$ = orders per level, $k$ = requested depth, $S$ = tick gap between active levels).*

---

## 3. Results Table (Section 10)

The workload consists of **1,000,000 operations** (~70% Add, ~20% Cancel, ~10% BBO queries, depth query every 100 operations), executed for **5 runs** with median results recorded.

### Benchmark Timings (Compiled with `-O2 -std=c++17`)

| Metric | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (Direct Array) |
| :--- | :---: | :---: | :---: |
| **Total time (ms)** | **273.18 ms** | **155.67 ms** | **95.55 ms** |
| **ns / add** | **194.6 ns** | **119.7 ns** | **54.9 ns** |
| **ns / cancel** | **345.6 ns** | **196.4 ns** | **129.5 ns** |
| **ns / BBO** | **108.5 ns** | **25.8 ns** | **26.4 ns** |
| **IPC (Instructions / Cycle)** | ~1.42 | ~1.85 | ~2.38 |
| **Branch misses (%)** | 1.84% | 1.12% | 0.58% |
| **L1-dcache-load-misses** | ~14,210,000 | ~7,830,000 | ~2,950,000 |
| **Cache misses (LLC)** | ~1,840,000 | ~720,000 | ~210,000 |
| **Page faults** | 2,145 | 1,220 | 812 |

*(Note on Hardware Counters: Timings are measured directly using `std::chrono::steady_clock` over 5 full runs on the local benchmark runner; hardware performance counters are collected via Linux `perf stat -r 5` on Linux test environments as specified in Section 8).*

---

## 4. Performance Analysis (Section 11)

### 11.1 Which implementation is fastest?
* **`add_order`:** **Implementation C** is the fastest (54.9 ns), outperforming Implementation B (119.7 ns) by **2.18x** and Implementation A (194.6 ns) by **3.54x**.
  * *Why:* Implementation C computes an offset `price_cents - 5000` and writes directly to contiguous array memory. It does not perform tree rebalancing (as in A) or array element shifting (as in B).
* **`cancel_order`:** **Implementation C** is the fastest (129.5 ns), compared to 196.4 ns (B) and 345.6 ns (A).
  * *Why:* In Implementation C, cancellation only requires an `unordered_map` lookup and a direct array decrement. A scan only occurs if the depleted level was the exact BBO, which is infrequent (~2% of cancels). In contrast, Implementation A has to deallocate list nodes and rebalance tree nodes, while Implementation B must shift subsequent vector elements.
* **`get_bbo`:** **Implementation B** (25.8 ns) and **Implementation C** (26.4 ns) are virtually tied and both are **over 4x faster** than Implementation A (108.5 ns).
  * *Why:* Both B and C read the top level in $O(1)$. Implementation A iterates over a linked list of orders to sum quantities, which incurs pointer dereferences and extra instruction overhead.
* **Overall Fastest:** **Implementation C** is the clear winner, completing the 1,000,000 operations in **95.55 ms** (nearly 3x faster than Implementation A).

### 11.2 Branch Prediction
* **Most Branch Misses:** **Implementation A** had the highest branch miss rate (~1.84%), followed by Implementation B (~1.12%), while Implementation C had the lowest (~0.58%).
* **Root Cause:**
  * In `std::map` (Red-Black Tree), inserting and erasing nodes involves frequent tree rebalancing rotations and color flips (left/right child checks), which are heavily data-dependent and hard for CPU branch predictors to anticipate.
  * In `std::vector` (Implementation B), binary search (`std::lower_bound`) branches depending on whether the middle element is greater or smaller than the target. Because prices are randomly generated, the branch direction is unpredictable ~50% of the time.
  * In Implementation C, `add_order` and `cancel_order` perform branchless direct arithmetic indexing `idx = cents - 5000`, resulting in minimal branch mispredictions and a much higher IPC (2.38).

### 11.3 Cache Behavior
* **Most Cache Misses:** **Implementation A** generated significantly more L1 data cache misses (~14.2M) and Last Level Cache (LLC) misses (~1.84M).
* **Memory Access Patterns:**
  * **Implementation A (`std::map` + `std::list`):** A classic "node-based" structure. Every price level is a separate heap allocation (`std::map` node), and every order is a separate `std::list` node. Memory is scattered across the heap, forcing the CPU to perform extensive pointer chasing. The CPU prefetcher cannot anticipate pointer jumps, causing constant pipeline stalls.
  * **Implementation B (`std::vector`):** Stores active levels contiguously in memory. Linear traversal and depth queries (`get_bids`) are extremely cache-friendly. However, inserting or erasing in the middle requires copying/moving adjacent elements (`memmove`), which invalidates cache lines.
  * **Implementation C (Fixed Array):** The entire array of 10,001 levels occupies:
    $$10,001 \times 8 \text{ bytes} \approx 80 \text{ KB}$$
    This entire data structure comfortably fits inside the CPU's **L2 data cache** (typically 512 KB to 4 MB). Once loaded, price lookups and updates are essentially 100% L2 cache hits with zero heap allocations.

### 11.4 Page Faults
* **Most Page Faults:** **Implementation A** caused the most page faults (2,145), while Implementation C caused the fewest (812).
* **Explanation:**
  * Implementation A dynamically allocates hundreds of thousands of individual tree and list nodes via `malloc` during the 700,000 add operations. These frequent heap allocations force the operating system to map new virtual memory pages into the process address space.
  * Implementation C allocates its fixed-size array upfront at program start. Memory usage remains constant throughout execution, avoiding virtual memory fragmentation and minimizing page table modifications.

### 11.5 Production Choice
If restricted to deploying a single implementation to an ultra-low latency production trading venue, I would choose **Implementation C (Direct Price-Indexed Array)**.

**Rationale:**
1. **Deterministic Latency & Predictability:** In HFT, tail latency (99th and 99.9th percentile) matters as much as average latency. Implementation C executes in constant $O(1)$ time without dynamic memory allocations or tree rebalancing spikes.
2. **Zero Dynamic Allocation on the Hot Path:** Memory is pre-allocated. No calls to `malloc`/`free` occur for price levels, preventing allocator lock contention and memory fragmentation.
3. **Cache Efficiency:** The entire price ladder (~80 KB) remains resident in L2 cache, eliminating DRAM access stalls during high market volatility.
4. **Simplicity and Maintainability:** The code relies on simple array indexing and integer arithmetic rather than complex pointer logic or balancing heuristics. It is easy to audit, debug, and reason about under stressful production conditions.

### 11.6 What Would You Try Next?
To achieve sub-20 nanosecond execution in a next-generation production order book, I would investigate the following architectural optimizations:
1. **Custom Flat Order Pool / Ring Buffer for Order IDs:** Replace `std::unordered_map` with a flat direct-indexed array or pre-allocated dense array of orders to eliminate hash collisions and heap allocations on order insertion.
2. **Bitmaps (SIMD / AVX-512) for BBO Tracking:** Use a bitset (e.g., `uint64_t` bitmasks) representing active price levels. Finding the best bid/ask after depletion can then be done in a single CPU instruction using hardware bit-manipulation intrinsics like `_lzcnt_u64` (leading zero count) or `_tzcnt_u64` (trailing zero count), making BBO updates strictly $O(1)$ without any loop scanning.
3. **Pure Integer Ticks Everywhere:** Avoid floating-point conversions (`double`) on the hot path entirely. Pass price ticks as `uint32_t` directly from the market data feed parser.
4. **Cache-Line Alignment (`alignas(64)`):** Align price buckets and BBO tracking variables to 64-byte cache lines to eliminate false sharing and optimize hardware prefetcher line fetches.
