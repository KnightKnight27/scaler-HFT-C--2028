# Implementation B: Priority Queue & Hash Map (Heap with Lazy Deletion)

## 1. Overview & Data Structures Used
Implementation B attempts to improve heap locality and push speed by replacing the balanced Red-Black Tree with standard binary heaps (`std::priority_queue`), combined with hash maps for order tracking and depth volume:
- **Per-Order Storage:** `std::unordered_map<uint64_t, OrderInfo>` storing order metadata (`side`, `price_tick`, `qty`, `bool active`). When an order is cancelled, `active` is toggled to `false` (**lazy deletion**).
- **Price Tracking (Heaps):**
  - `std::priority_queue<BidHeapNode> bid_pq_` (Max-heap by price tick).
  - `std::priority_queue<AskHeapNode, ..., std::greater<AskHeapNode>> ask_pq_` (Min-heap by price tick).
- **Volume Tracking per Level:**
  - `std::unordered_map<int, long> bid_quantities_`
  - `std::unordered_map<int, long> ask_quantities_`
- **Top Cleaning Mechanism:** `clean_bids()` and `clean_asks()` lazily pop inactive orders or exhausted price levels from the top of the heaps during BBO or depth inspection.

---

## 2. Algorithmic Complexity

| Operation | Complexity | Explanation |
| :--- | :--- | :--- |
| `add_order` | $O(\log N)$ | Pushes new node into binary heap ($O(\log N)$) and updates hash maps ($O(1)$ average). |
| `cancel_order` | $O(1)$ | Hash lookup to mark `active = false` and decrement level quantity in `bid_quantities_` / `ask_quantities_`. Deletion from the heap is deferred. |
| `get_bbo` | Amortized $O(1)$ (Worst $O(K \log N)$) | Accesses heap top. If top elements are cancelled, pops $K$ stale elements ($O(K \log N)$). |
| `get_bids` / `get_asks` | $O(P \log P)$ | Because hash maps are unsorted, extracting top $D$ levels requires copying all active price levels ($P$) into a `std::vector` and sorting them. |

---

## 3. How to Build and Run
From the `low_latency_order_book/` directory:

```bash
# Compile with -O2 optimization
g++ -O2 -std=c++17 src/main.cpp src/order_book_a.cpp src/order_book_b.cpp -Iinclude -o bench

# Verify correctness against Implementation A
./bench --verify

# Run Benchmark (1,000,000 operations, median of 5 runs)
./bench --impl B

# Run Linux perf stat (page faults, task clock)
perf stat -r 5 \
-e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
./bench --impl B
```

---

## 4. Empirical Benchmark Results (Median of 5 Runs - Linux/WSL)

- **Total Execution Time:** `6,115.08 ms` (~37x slower than Implementation A)
- **Avg Latency per `add_order`:** `131.478 ns`
- **Avg Latency per `cancel_order`:** `308.034 ns`
- **Avg Latency per `get_bbo`:** `48.668 ns`

**Linux `perf stat` Metrics (5 runs):**
- **Task-Clock:** `30,807.85 msec` (±0.58%)
- **Page Faults:** `33,700` (+48% higher than Implementation A's 22,774)
- **Time Elapsed:** `30.818 s`

**Final State Verification:**
- **Best Bid:** `99.99 x 2722`
- **Best Ask:** `100.01 x 2525`
- **Checksum:** `25247` *(Exact parity with Implementation A)*

---

## 5. Hardware & Architectural Analysis

### 1. The Add Operation Improvement (~131 ns vs 139 ns)
Binary heaps in `std::priority_queue` are backed by contiguous `std::vector` memory. During `add_order`, pushing onto a vector offers better spatial cache locality than inserting a newly allocated node into a Red-Black Tree, yielding a slight latency reduction for order entry.

### 2. Lazy Deletion Memory Bloat & Stale Node Accumulation
Because cancellations do not immediately delete items inside the heap, canceled orders remain in memory until they bubble up to the top. Over 1,000,000 mixed operations (with 200,000 cancellations), the heaps grow significantly larger than the active working set, resulting in:
- Deeper heap levels ($O(\log N)$ degradation).
- Higher memory footprint and cache pollution.
- Elevated page faults (`33,700` vs `22,774`).

### 3. The Depth Query Bottleneck (`get_bids` / `get_asks`)
The massive difference in overall execution time (`6,115 ms` vs `165 ms`) stems directly from market depth queries called periodically (every 100 operations):
- In **Implementation A**, `std::map` keeps levels pre-sorted, making `get_bids(10)` an instant $O(D)$ iterator crawl.
- In **Implementation B**, price quantities reside in unsorted `std::unordered_map`. Every query copies all active levels into a vector and calls `std::sort`, turning a microsecond operation into a heavy, multi-millisecond bottleneck.

---

## 6. Pros and Cons

### Pros
- Contiguous array storage for binary heaps provides better cache locality during insertions than node-based trees.
- Fast, non-blocking order cancellation logic (simply mark boolean flag and update quantity map).
- Good choice for matching engines that *only* match top of the book and rarely export full market depth.

### Cons
- **Severe Depth Query Penalty:** Extracting Level-2 / Level-3 market depth requires full container copying and sorting ($O(P \log P)$).
- **Unbounded Heap Bloat:** Cancelled orders linger in the priority queue indefinitely if market prices move away from them, wasting memory.
- **`get_bbo` Jitter:** When multiple cancelled orders sit at the top of the heap, `get_bbo()` experiences non-deterministic latency spikes while popping them.
- **Multiple Hash Maps:** Requires maintaining both order tracking and quantity tracking hash maps, increasing hash overhead and collisions.

---

## 7. Comparative Verdict: Is Implementation B Better or Worse?

### **Verdict: WORSE for general HFT Order Book requirements.**

| Dimension | Implementation A (Map + Hash) | Implementation B (PQ + Hash) | Winner |
| :--- | :--- | :--- | :--- |
| **Total Runtime (1M ops)** | **165.40 ms** | 6,115.08 ms | **Implementation A (~37x faster)** |
| **`add_order` Latency** | 138.96 ns | **131.48 ns** | **Implementation B (~5% faster)** |
| **`cancel_order` Latency**| **234.32 ns** | 308.03 ns | **Implementation A (~24% faster)** |
| **`get_bbo` Latency** | **20.35 ns** | 48.67 ns | **Implementation A (~2.4x faster)** |
| **Depth Queries (`get_depth`)**| **$O(D)$ instantaneous** | $O(P \log P)$ sort required | **Implementation A (Dramatically better)** |
| **Memory / Page Faults** | **22,774 faults** | 33,700 faults | **Implementation A (Lower memory churn)** |

**Conclusion:**  
While binary heaps theoretically optimize top-of-book retrieval with array-backed storage, the reality in production order books is that **market depth (L2 book) dissemination and deterministic latency are mandatory**. Implementation B pays an exorbitant cost to reconstruct sorted depth from hash maps, and suffers from memory bloat due to lazy deletion. Therefore, Implementation B is **strictly inferior** to Implementation A as a complete order book solution, setting the stage for **Implementation C (Fixed-Size Array + Branchless Binary Search)** to solve both issues.
