# Low-Latency Order Book (Capstone Project)

**Team Members:** Ayush Kumar Patra and Nandani Kumari  
**Email:** ayush.24bcs10474@sst.scaler.com and nandani.24bcs10317@sst.scaler.com  

---

## 1. Project Overview
This project implements a single-instrument resting order book supporting bids and asks with tick sizes of `0.01` (represented internally as integers to avoid floating-point overhead). The goal is to design, implement, benchmark, and analyze three increasingly optimized data-structure architectures to understand low-latency C++ systems programming, cache locality, and hardware-level performance.

---

## 2. Project Structure
```text
low_latency_order_book/
├── include/
│   ├── order_book.hpp        # Common data structures (Side, Level, BBO)
│   ├── order_book_a.hpp      # Implementation A (Sorted Map & Hash Map)
│   ├── order_book_b.hpp      # Implementation B (Priority Queue + Hash Map)
│   └── order_book_c.hpp      # Implementation C (Direct-Indexed Price Array + BBO Tracking)
├── src/
│   ├── main.cpp              # Unified benchmark & verification harness
│   ├── order_book_a.cpp      # Implementation A logic
│   ├── order_book_b.cpp      # Implementation B logic
│   └── order_book_c.cpp      # Implementation C logic
├── docs/
│   ├── implementation_a_notes.md  # Detailed design notes for Approach A
│   ├── implementation_b_notes.md  # Detailed design notes for Approach B
│   └── implementation_c_notes.md  # Detailed design notes for Approach C
├── CMakeLists.txt             # Build configuration
└── README.md                  # Project documentation
```

---

## 3. How to Build the Project
Compiled with `-O2` optimization as required by the assignment rules:

```bash
# Navigate to the source directory
cd low_latency_order_book

# Compile using g++ directly
g++ -O2 -std=c++17 src/main.cpp src/order_book_a.cpp src/order_book_b.cpp src/order_book_c.cpp -Iinclude -o bench
```

---

## 4. How to Run Correctness Tests
To verify that all implementations pass and maintain exact state parity:

```bash
./bench --verify
```

---

## 5. How to Run Benchmarks
The benchmark runs 1,000,000 mixed operations (70% adds, 20% cancels, 10% BBO queries, with depth queries every 100 ops) across 5 runs and reports the median:

```bash
# Benchmark Implementation A
./bench --impl A

# Benchmark Implementation B
./bench --impl B

# Benchmark Implementation C
./bench --impl C

# Capture hardware & OS performance counters via Linux perf
perf stat -r 5 \
-e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
./bench --impl C
```

---

## 6. Empirical Benchmark Results (Median of 5 Runs - Linux/WSL)

| Metric | Implementation A (Map + Hash) | Implementation B (Heap + Hash) | Implementation C (Direct Array + BBO) | Best Performer |
| :--- | :--- | :--- | :--- | :--- |
| **Total time (ms)** | 165.40 ms | 6,115.08 ms *(~37x slower)* | **77.45 ms** *(74.57 ms quiet)* | **Implementation C (>2.1x vs A, >78x vs B)** |
| **ns / add** | 138.96 ns | 131.48 ns | **45.48 ns** | **Implementation C (~3x faster)** |
| **ns / cancel** | 234.32 ns | 308.03 ns | **116.41 ns** | **Implementation C (~2x faster)** |
| **ns / BBO** | 20.35 ns | 48.67 ns | **18.65 ns** | **Implementation C (Fastest)** |
| **Task Clock (5 runs)** | 943.79 ms | 30,807.85 ms | **496.78 ms** | **Implementation C (Half CPU of A, 1/62nd of B)** |
| **Page faults** | 22,774 | 33,700 *(+48%)* | **21,254** | **Implementation C (Lowest memory churn)** |
| **Elapsed Real Time** | 0.950 s | 30.818 s | **0.501 s** | **Implementation C (Fastest)** |
| **Correctness Parity** | `Checksum: 25247` | `Checksum: 25247` | `Checksum: 25247` | **100% Exact Agreement** |

> *Note: Hardware PMU counters (cycles, instructions, cache/branch misses) require hypervisor vPMU support in WSL2; OS metrics like task-clock and page-faults were captured via `perf stat`.*

---

## 7. Implementation A: Sorted Map & Hash Map (Baseline)

### Design Summary:
- **Per-Order Storage:** `std::unordered_map<uint64_t, OrderDetails>` for $O(1)$ average-time cancellation lookups.
- **Per-Price Storage:** Two `std::map` containers (bids sorted descending, asks sorted ascending) backed by Red-Black Trees.

### Hardware & Architectural Analysis:
- **`get_bbo` Performance (~20.35 ns):** Extremely fast because Red-Black trees maintain sorted keys, allowing `.begin()` to instantly reference the best bid/ask.
- **`cancel_order` Latency (~234.32 ns):** Requires a hash lookup, a tree traversal, quantity subtraction, and conditional tree node deletion/rebalancing.
- **Cache Inefficiency & Allocations:** Tree nodes and unordered map nodes are allocated dynamically on the heap (`new`/`malloc`), producing 22,774 page faults during operation runs and frequent cache misses due to pointer chasing.

---

## 8. Implementation B: Priority Queue & Hash Map (Heap with Lazy Deletion)

### Design Summary:
- **Per-Order Storage:** `std::unordered_map<uint64_t, OrderInfo>` storing order metadata and `bool active` for $O(1)$ lazy cancellation.
- **Price Heaps:** `std::priority_queue` max-heap for bids and min-heap for asks.
- **Volume Tracking:** Two `std::unordered_map<int, long>` tracking aggregate volume per price level.
- **Lazy Top Cleaning:** Stale, cancelled orders at the top of the heaps are purged on-demand during `get_bbo()` and depth queries.

### Hardware & Architectural Analysis:
- **`add_order` Latency (~131.48 ns):** Pushing to a `std::priority_queue` (backed by a contiguous vector) provides better spatial cache locality than inserting a newly allocated node into a Red-Black tree.
- **Lazy Deletion Heap Bloat:** Cancelled orders remain in the heap until they naturally surface at the top. This causes heap size to grow unbounded over time, degrading cache locality and increasing page faults from 22,774 to 33,700 (+48%).
- **The Depth Query Bottleneck:** Every 100 ops, `get_bids(10)` and `get_asks(10)` are called. Because price quantities reside in unsorted hash maps, extracting the top 10 requires copying all active price levels into a vector and sorting them ($O(P \log P)$). This single bottleneck causes total runtime to balloon to **6,115.08 ms** (~37x slower than Implementation A).

### Pros & Cons:
- **Pros:** Fast $O(\log N)$ heap push with array cache locality; non-blocking $O(1)$ cancellation marking.
- **Cons:** Catastrophic depth query overhead ($O(P \log P)$ vector sort); heap memory bloat from unpurged canceled orders; non-deterministic latency jitter in `get_bbo()`.

### Verdict: Is Implementation B Better or Worse?
**Worse overall for real-world trading.**  
While binary heaps offer a modest ~5% speedup on order entry (`add_order`), they severely compromise market depth retrieval, increase memory churn (+48% page faults), and make overall system throughput 37x slower.

---

## 9. Implementation C: Direct-Indexed Price Array & BBO Tracking (Optimized HFT Engine)

### Design Summary:
- **Price Level Storage:** Pre-allocated contiguous `std::vector<long> price_quantities_` spanning tick indices 0 to 15,000. Price tick $T$ directly addresses index $T$ in constant $O(1)$ time without searching.
- **BBO Tracking:** Integer variables `best_bid_tick_` and `best_ask_tick_` continuously track the inner market boundaries.
- **Per-Order Storage:** `std::unordered_map<uint64_t, OrderInfo>` providing $O(1)$ order cancellation lookups.

### Key Differentiators & Why It Worked:
1. **Direct Array Indexing vs Search:** Adding volume to a price level is a direct memory addition (`price_quantities_[tick] += qty`), requiring zero pointer chasing, zero tree balancing, and zero heap operations. This reduced `add_order` latency to an industry-grade **`45.48 ns`** (~3x faster than A and B).
2. **Dense L1/L2 Cache Residency:** The entire 15,000-element price vector occupies ~120 KB, fitting easily inside CPU L2 cache, with the active bid-ask spread residing continuously in L1 data cache.
3. **Zero Dynamic Allocation on Price Levels:** Price slots are pre-allocated upfront. Unlike `std::map`, price updates never invoke the heap memory allocator, achieving the lowest page faults (`21,254`) and lowest CPU time (`496.78 ms`).
4. **Instant Depth Streaming:** Market depth queries (`get_bids`/`get_asks`) simply scan linearly down or up the contiguous array from the BBO registers. The CPU hardware stream prefetcher fetches adjacent cache lines with zero cache misses and zero sorting.

### Pros & Cons:
- **Pros:** Dominates every metric (>2x faster overall, 3x faster adds, 2x faster cancels); zero allocation jitter; deterministic $O(1)$ BBO retrieval; hardware-native cache locality.
- **Cons:** Memory footprint scales with the absolute price range of the instrument (ideal for bounded liquid markets, requires sparse block tables for unbounded instruments).

### Verdict: Is Implementation C Better?
**The Definitive Winner.** Implementation C dramatically outperforms both A and B in latency, throughput, cache efficiency, and memory stability.

---

## 10. Deep Architectural Comparison: Implementation A vs. B vs. C

| Architectural Aspect | Implementation A (Sorted Map) | Implementation B (Priority Queue) | Implementation C (Direct Array) |
| :--- | :--- | :--- | :--- |
| **Price Storage** | `std::map` (Red-Black Tree) | `std::priority_queue` (Binary Heap) + Hash Map | Pre-allocated `std::vector<long>` (Flat Array) |
| **Order Tracking** | `std::unordered_map` | `std::unordered_map` with `bool active` | `std::unordered_map` |
| **`add_order` Mechanism** | $O(\log P)$ Tree search & node allocation | $O(\log N)$ Heap push + hash update | **$O(1)$ Direct index arithmetic** |
| **`cancel_order` Mechanism** | Hash lookup + $O(\log P)$ tree erasure | Hash lookup + $O(1)$ lazy flag toggle | **Hash lookup + $O(1)$ array decrement** |
| **`get_bbo` Mechanism** | $O(1)$ tree `.begin()` iterator read | Amortized $O(1)$ / Worst $O(K \log N)$ pop stale | **$O(1)$ Direct register & array read** |
| **Depth Retrieval (`get_bids/asks`)** | $O(D)$ tree iterator traversal | $O(P \log P)$ full map copy & `std::sort` | **$O(D)$ contiguous memory scan** |
| **Memory Allocation** | Dynamic node allocation on every new level | Dynamic vector growth on heap pushes | **Zero dynamic allocations on critical path** |
| **CPU Cache Efficiency** | Poor (scattered heap pointer chasing) | Moderate for adds, poor for depth | **Near-Perfect (dense contiguous cache lines)** |
| **Total 1M Ops Runtime** | 165.40 ms | 6,115.08 ms | **77.45 ms (Winner)** |

---

### Why Did Implementation C Outperform A and B So Dramatically?

1. **Eliminating the Cost of Pointer Indirection (vs Implementation A):**  
   `std::map` stores every price level as an isolated heap node containing left, right, and parent pointers plus balancing metadata. Navigating or inserting into this tree forces the CPU to stall while dereferencing pointers from arbitrary heap locations ("pointer chasing"). Implementation C eliminates pointers entirely by converting the price to a numeric index.

2. **Avoiding the Depth Sorting Trap & Heap Bloat (vs Implementation B):**  
   While `std::priority_queue` provides array backing for the top element, it inherently destroys the rest of the book's order. When downstream market makers require Level-2 depth, Implementation B is forced to dump its unsorted hash map into a vector and sort it from scratch. In high-frequency workloads where depth is queried continuously, this sorting overhead completely drowns out any algorithmic benefits. Furthermore, lazy deletion causes heaps to accumulate hundreds of thousands of dead orders, inflating memory consumption.

3. **Sympathy with Modern CPU Architecture:**  
   Modern CPUs achieve peak throughput when memory access is linear and predictable. Implementation C stores prices in a dense, contiguous array that matches CPU cache line granularity (64 bytes = 8 price ticks per cache line). Sequential depth scans trigger CPU hardware stream prefetchers automatically, executing with zero cache miss stalls and delivering true low-latency performance.
