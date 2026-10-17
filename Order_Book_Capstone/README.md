# Capstone: Low-Latency Single-Instrument Order Book

**SST Email:** [minesh.24bcs10029@sst.scaler.com]  
**Roll Number:** [24BCS10029]  

## 1. Overview
This project implements, verifies, benchmarks, and profiles three distinct single-threaded C++17 order-book architectures for a single financial instrument. 

### Domain Constraints
- **Tick Size:** `0.01` (all prices are exact multiples of `0.01`)
- **Bid Price Range:** `[50.00, 99.99]`
- **Ask Price Range:** `[100.01, 150.00]`
- **Mid Price:** `100.00` (10,000 valid price levels in total)
- **Execution Model:** Resting order book only (no order matching or trade generation; orders rest until cancelled). 

---

## 2. Project Structure & Build Instructions

### Directory Layout
- `include/types.hpp`: Core structs (`Side`, `Level`, `BBO`) & exact-integer tick conversion helpers.
- `include/order_book_a.hpp`: Impl A (Red-Black Tree + Hash Map).
- `include/order_book_b.hpp`: Impl B (Binary Heap + Lazy Deletion + Hash Map).
- `include/order_book_c.hpp`: Impl C (Direct-Indexed Contiguous Array + Cached Best Pointers).
- `src/verify.hpp`: Step-by-step deterministic correctness checker.
- `src/main.cpp`: Benchmark CLI harness and workload generator.

### Build & Run Commands
```bash
# Build
g++ -std=c++17 -O2 src/main.cpp -o bench

# Verify Correctness
./bench --verify

# Run Benchmarks
./bench --impl A
./bench --impl B
./bench --impl C

```

---

## 3. The Three Implementations & Complexity

### Interface & Price Representation

While the public API accepts `double` for full compatibility, prices are safely converted at the API boundary into 32-bit integers (`int32_t` ticks via `std::llround(price * 100.0)`). This prevents IEEE-754 precision drift during lookups and shrinks cache footprint.

### Implementation A: Standard Associative Tree (`std::map` + `std::unordered_map`)

* **Data Structures:** `std::map` (Red-Black tree) for price levels, sorted via `std::greater` for Bids and `std::less` for Asks. `std::unordered_map` for order ID lookup.
* **Storage:** Orders are nodes in the hash map; price levels are nodes in the tree.
* **Best Price:** Found in $O(1)$ amortized time by reading `.begin()`.
* **Cancel:** $O(\log P)$ time to find the price node and decrement/erase it.

### Implementation B: Binary Heap with Lazy Deletion (`std::priority_queue`)

* **Data Structures:** `std::priority_queue` (backed by a contiguous `std::vector`) tracks prices. `std::unordered_map` tracks live quantities per price level.
* **Storage:** A price tick is only pushed to the heap when its live quantity transitions from `0` to `> 0`.
* **Best Price:** Found by inspecting `.top()`. If the top price level's quantity is `0` (due to prior cancels), it is lazily popped until a valid price is exposed.
* **Cancel:** $O(1)$ average time. The order is erased and the quantity decremented in the hash map without touching the heap.

### Implementation C: Custom High-Frequency Engine (Direct-Indexed Arrays)

* **Data Structures:** Fixed-size `std::array<long, 5000>` for both Bids and Asks. Cached integer offsets track the absolute best prices.
* **Storage:** Price levels map directly to array indices via `tick - 5000` (bids) or `tick - 10001` (asks).
* **Best Price:** Instantaneous $O(1)$ read using the cached `mBestBidOffset` and `mBestAskOffset`.
* **Cancel:** $O(1)$ direct array index subtraction. Zero heap allocations or pointer chasing.

### Complexity Table

| Operation | Impl A (`std::map`) | Impl B (`priority_queue`) | Impl C (Custom Array) |
| --- | --- | --- | --- |
| **add_order** | $O(\log P)$ | $O(1)$ avg / $O(\log H)$ | $O(1)$ |
| **cancel_order** | $O(\log P)$ | $O(1)$ average | $O(1)$ |
| **get_bbo** | $O(1)$ | $O(1)$ amortized | $O(1)$ |
| **get_bids(K)** | $O(K)$ | $O(K \log H)$ amortized | $O(K)$ scan |

---

## 4. Benchmark & `perf stat` Results

### Correctness Output

```text
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.

```

### Raw perf stat Outputs

#### Implementation A
```text
Implementation A Benchmark Results:
  Total Time (Median of 5 runs): 608.594 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 608594
Implementation A Benchmark Results:
  Total Time (Median of 5 runs): 562.561 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 562561
Implementation A Benchmark Results:
  Total Time (Median of 5 runs): 311.892 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 311892
Implementation A Benchmark Results:
  Total Time (Median of 5 runs): 302.933 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 302932
Implementation A Benchmark Results:
  Total Time (Median of 5 runs): 280.174 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 280173

 Performance counter stats for './bench --impl A' (5 runs):

          2,300.90 msec task-clock:u                                                            ( +- 16.74% )
     4,749,584,752      cycles:u                                                                ( +-  2.73% )  (85.68%)
     3,476,396,026      instructions:u                                                          ( +-  0.07% )  (85.72%)
       645,718,410      branches:u                                                              ( +-  0.09% )  (85.72%)
        39,177,730      branch-misses:u                                                         ( +-  0.08% )  (85.72%)
        37,932,440      cache-misses:u                                                          ( +-  0.48% )  (85.73%)
        80,084,704      L1-dcache-load-misses:u                                                 ( +-  0.08% )  (85.72%)
         3,180,484      dTLB-load-misses:u                                                      ( +-  6.71% )  (85.70%)
             9,046      page-faults:u                                                           ( +-  2.28% )

       2.306033419 +- 0.385759531 seconds time elapsed  ( +- 16.73% )
```

#### Implementation B
```text
Implementation B Benchmark Results:
  Total Time (Median of 5 runs): 355.314 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 355313
Implementation B Benchmark Results:
  Total Time (Median of 5 runs): 392.531 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 392530
Implementation B Benchmark Results:
  Total Time (Median of 5 runs): 360.593 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 360593
Implementation B Benchmark Results:
  Total Time (Median of 5 runs): 202.988 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 202988
Implementation B Benchmark Results:
  Total Time (Median of 5 runs): 167.719 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 167718

 Performance counter stats for './bench --impl B' (5 runs):

          1,706.90 msec task-clock:u                                                            ( +- 13.97% )
     2,905,285,689      cycles:u                                                                ( +-  1.42% )  (85.63%)
     3,646,447,091      instructions:u                                                          ( +-  0.05% )  (85.74%)
       603,030,672      branches:u                                                              ( +-  0.06% )  (85.72%)
         7,485,241      branch-misses:u                                                         ( +-  0.27% )  (85.73%)
        26,811,193      cache-misses:u                                                          ( +-  0.51% )  (85.74%)
        46,151,422      L1-dcache-load-misses:u                                                 ( +-  0.34% )  (85.73%)
         3,103,135      dTLB-load-misses:u                                                      ( +-  3.70% )  (85.71%)
            16,759      page-faults:u                                                           ( +-  1.79% )

       1.714092387 +- 0.239244002 seconds time elapsed  ( +- 13.96% )
```

#### Implementation C
```text
Implementation C Benchmark Results:
  Total Time (Median of 5 runs): 231.682 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 231681
Implementation C Benchmark Results:
  Total Time (Median of 5 runs): 226.23 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 226230
Implementation C Benchmark Results:
  Total Time (Median of 5 runs): 225.805 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 225804
Implementation C Benchmark Results:
  Total Time (Median of 5 runs): 225.552 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 225552
Implementation C Benchmark Results:
  Total Time (Median of 5 runs): 169.119 ms
  Final BBO -> Bid: None | Ask: None
  Checksum: 169118

 Performance counter stats for './bench --impl C' (5 runs):

          1,316.06 msec task-clock:u                                                            ( +-  5.50% )
     1,953,884,714      cycles:u                                                                ( +-  1.31% )  (85.70%)
     3,017,158,220      instructions:u                                                          ( +-  0.18% )  (85.69%)
       513,068,669      branches:u                                                              ( +-  0.16% )  (85.68%)
         7,144,779      branch-misses:u                                                         ( +-  0.25% )  (85.70%)
        17,420,726      cache-misses:u                                                          ( +-  0.64% )  (85.72%)
        30,451,886      L1-dcache-load-misses:u                                                 ( +-  0.15% )  (85.78%)
         2,950,697      dTLB-load-misses:u                                                      ( +-  2.23% )  (85.72%)
             8,898      page-faults:u                                                           ( +-  1.64% )

       1.322232026 +- 0.072984232 seconds time elapsed  ( +-  5.52% )
```

### Summary Results Table (1,000,000 Operations, Seed = 42)

| Metric | Impl A (`std::map`) | Impl B (`priority_queue`) | Impl C (Array) |
| --- | --- | --- | --- |
| **Total time (ms)** | 311.892 | 355.314 | 225.805 |
| **IPC** | 0.732 | 1.255 | 1.544 |
| **Branch misses (%)** | 6.07% | 1.24% | 1.39% |
| **L1-dcache-load-misses** | 80,084,704 | 46,151,422 | 30,451,886 |
| **Cache misses (LLC)** | 37,932,440 | 26,811,193 | 17,420,726 |
| **Page faults** | 9,046 | 16,759 | 8,898 |

---

## 5. Performance Analysis

### 5.1 Fastest Implementation

From the collected results, **Implementation C** is the fastest with median total time of **225.8 ms**, followed by **Implementation A** at **311.9 ms**, and **Implementation B** at **355.3 ms**. By mapping prices directly to physical array indices (`tick - 5000`), `add_order` and `cancel_order` execute as simple arithmetic offsets and $O(1)$ memory writes, bypassing $O(\log N)$ structural overhead.

### 5.2 Branch Prediction

**Implementation A** suffers the highest branch miss rate at **~6.07%** due to data-dependent comparisons in Red-Black tree traversal. Implementations B (**1.24%**) and C (**1.39%**) have much lower branch miss rates; C's direct addressing keeps the hot path largely branch-predictable.

### 5.3 Cache Behavior & CPU Locality

L1 dcache load misses follow A (80.08M) > B (46.15M) > C (30.45M), and LLC cache misses follow the same order (37.93M, 26.81M, 17.42M). This reflects pointer-chasing in `std::map` nodes (A), partially contiguous heap in B but still fragmented node structures, and C's fully contiguous fixed arrays providing superior spatial locality. IPC also confirms this: C achieves 1.54, B 1.26, A 0.73, indicating C does more useful work per cycle.

### 5.4 Page Faults

Implementation B produced the most page faults (**16,759**), followed by A (**9,046**) and C (**8,898**). This contradicts the initial expectation that A alone would dominate; the measurements show B triggers more minor faults, while C minimizes faults by allocating its fixed domain layout upfront.

### 5.5 Production Choice

For a real trading system, **Implementation C** is the undisputed choice. Beyond having the lowest mean latency, it offers absolute **predictability** (strict worst-case execution time with zero latency spikes from lazy deletion cascades or tree rebalancing), zero runtime heap fragmentation, and fits beautifully into CPU L1/L2 caches.
