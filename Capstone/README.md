# Capstone: Low-Latency Order Book Benchmark & Analysis

## Student Details
- **Email:** `24bcs10464@sst.scaler.com`
- **Roll Number:** `24BCS10464`
- **Name:** Krushna Sonawane
- **GitHub:** [AGENT-BABA](https://github.com/AGENT-BABA)

---

## 1. Project Overview

This capstone project implements, benchmarks, and profiles a high-performance resting order book for a single instrument across **three distinct data-structure designs**:

1. **Implementation A (`OrderBookMap`):** Standard associative container using balanced binary search trees (`std::map`, Red-Black tree).
2. **Implementation B (`OrderBookVector`):** Contiguous flat sorted array (`std::vector<Level>`) with binary search (`std::lower_bound`).
3. **Implementation C (`OrderBookDirect`):** Custom high-frequency trading design using a fixed direct price-indexed flat array (`std::array<long, 10001>`) with cached Best-Bid-and-Offer (BBO) indices.

### Instrument Rules & Constraints
- **Tick size:** `0.01` (all prices are exact multiples of 0.01).
- **Bid range:** `[50.00, 99.99]` (5,000 to 9,999 ticks).
- **Ask range:** `[100.01, 150.00]` (10,001 to 15,000 ticks).
- **Mid price:** `100.00`.
- **Price Levels:** 10,000 possible active price levels.
- **Order Model:** Resting book only (no matching engine or trade generation). Orders remain until explicitly cancelled.
- **Concurrency:** Single-threaded execution. C++ standard library only (no Boost).

---

## 2. Architecture & Data Structures

```
                          +------------------------+
                          |   IOrderBook Interface  |
                          +------------------------+
                                      |
         +----------------------------+----------------------------+
         |                                                         |
+------------------+                              +-------------------------------+
| Implementation A |                              |       Implementation C        |
|  (std::map Tree) |                              | (Direct Price-Indexed Array)  |
+------------------+                              +-------------------------------+
| - Red-Black Node |                              | - 10,001 slot flat array      |
| - Pointer chasing|                              | - Direct O(1) tick indexing   |
| - Heap per level |                              | - Cached BBO indices (O(1))   |
+------------------+                              +-------------------------------+
         |
+------------------+
| Implementation B |
| (Sorted Vector)  |
+------------------+
| - Contiguous RAM |
| - lower_bound BS |
| - Cache-friendly |
+------------------+
```

### Big-O Complexity Comparison

| Operation | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (`OrderBookDirect`) |
| :--- | :--- | :--- | :--- |
| **`add_order`** | $\mathcal{O}(\log L)$ | $\mathcal{O}(\log L + L)$ (insert & shift) | $\mathcal{O}(1)$ |
| **`cancel_order`** | $\mathcal{O}(\log L)$ | $\mathcal{O}(\log L + L)$ (erase & shift) | $\mathcal{O}(1)$ avg / $\mathcal{O}(K)$ level step |
| **`get_bbo`** | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ |
| **`get_bids(D)`** | $\mathcal{O}(D)$ | $\mathcal{O}(D)$ (contiguous slice) | $\mathcal{O}(D)$ (dense scan) |
| **`get_asks(D)`** | $\mathcal{O}(D)$ | $\mathcal{O}(D)$ (contiguous slice) | $\mathcal{O}(D)$ (dense scan) |

*(Where $L$ is the number of active price levels, and $D$ is the requested market depth).*

---

## 3. How to Build & Run (Ubuntu & Windows)

The project supports both **Linux/Ubuntu** and **Windows** out of the box with zero external dependencies.

### Option A: Ubuntu / Linux (Bash)

#### 1. Compile
```bash
# Using g++ directly (-O2 optimization):
g++ -O2 -std=c++17 Capstone/bench.cpp -o Capstone/bench

# Or using Make:
cd Capstone && make
```

#### 2. Run Correctness Verification
```bash
./Capstone/bench --verify
```

#### 3. Run Benchmarks
```bash
# Benchmark individual implementations
./Capstone/bench --impl A
./Capstone/bench --impl B
./Capstone/bench --impl C

# Or benchmark all three in sequence
./Capstone/bench --all
```

#### 4. Linux `perf` Profiling (As required by Brief Section 8)
```bash
# Profile CPU counters across 5 runs:
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./Capstone/bench --impl A

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./Capstone/bench --impl B

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./Capstone/bench --impl C

# Record and inspect hotspots:
perf record -g ./Capstone/bench --impl A && perf report
perf record -g ./Capstone/bench --impl B && perf report
perf record -g ./Capstone/bench --impl C && perf report
```

---

### Option B: Windows (PowerShell / MinGW)

#### 1. Compile
```powershell
# Using g++ directly (-O2 optimization):
g++ -O2 -std=c++17 Capstone/bench.cpp -o Capstone/bench

# Or using mingw32-make:
cd Capstone
mingw32-make
```

#### 2. Run Correctness Verification
```powershell
.\bench --verify
```

#### 3. Run Benchmarks
```powershell
# Benchmark individual implementations
.\bench --impl A
.\bench --impl B
.\bench --impl C

# Or benchmark all three in sequence
.\bench --all
```

*(Note for Windows: Linux `perf` hardware counter subsystem is specific to the Linux kernel. If profiling on Windows, standard `std::chrono::steady_clock` high-precision timers report exact nanosecond timings per operation, or you can run inside WSL2 / Ubuntu lab machines for `perf stat` counters).*

---

## 4. Correctness Results

Running `./bench --verify` executes the exact same 1,000,000 operation workload across all 3 implementations, verifying that BBO, bid/ask depth, prices, and quantities agree at every step:

```text
Generating 1,000,000 operation workload (Seed: 42)...
Generated 1010000 operations.

Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

---

## 5. Performance Benchmark Results

### Hardware & Environment
- **CPU:** 13th Gen Intel(R) Core(TM) i5-13450HX (10 Cores: 6 Performance + 4 Efficient, 16 Logical Processors)
- **Clock Speed:** Base 2.40 GHz / Max Turbo 4.60 GHz
- **Compiler:** `g++ 16.2.0` with `-O2 -std=c++17`
- **Workload:** 1,000,000 operations (70% Add, 20% Cancel, 10% BBO Query, plus `get_bids`/`get_asks` every 100 ops). Fixed Seed: 42.

### Summary Results Table (Median of 5 Trials)

| Metric | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (`OrderBookDirect`) |
| :--- | :--- | :--- | :--- |
| **Total Time (ms)** | **256.09 ms** | **212.67 ms** | **153.40 ms** *(Fastest)* |
| **ns / `add_order`** | 218.30 ns | 168.01 ns | **106.91 ns** *(~2x speedup)* |
| **ns / `cancel_order`** | 363.07 ns | 323.79 ns | **238.88 ns** *(~34% speedup)* |
| **ns / `get_bbo`** | 25.03 ns | 25.06 ns | **25.03 ns** |
| **Final BBO** | `99.99 x 2248` / `100.01 x 2220` | `99.99 x 2248` / `100.01 x 2220` | `99.99 x 2248` / `100.01 x 2220` |
| **Checksum** | `221063318` | `221063318` | `221063318` |

---

## 6. Performance Analysis (Sections 11.1 – 11.6)

### 11.1 Which Implementation is Fastest & Why?
- **Fastest for `add_order`:** **Implementation C (`OrderBookDirect`)** at **106.91 ns** (vs 168.01 ns for B and 218.30 ns for A).
  - *Reason:* Direct index calculation `idx = tick - 5000` requires zero comparisons, zero binary searches, and zero dynamic memory allocations. It is an immediate $O(1)$ integer addition in L2-cached memory.
- **Fastest for `cancel_order`:** **Implementation C (`OrderBookDirect`)** at **238.88 ns** (vs 323.79 ns for B and 363.07 ns for A).
  - *Reason:* Once the order record is retrieved from the ID map, decrementing quantity is an instant array write. A scan is only triggered if the cleared level was the active BBO.
- **Fastest for `get_bbo`:** All three achieve **~25 ns**, but Implementation C guarantees instant access to cached register variables (`best_bid_idx_` and `best_ask_idx_`) without traversing tree nodes or container metadata.
- **Total Throughput:** Implementation C completed 1,000,000 operations in **153.40 ms** (**~40% faster than Implementation A**).

---

### 11.2 Branch Prediction
- **Implementation A (`std::map`):** Tree traversal dynamically chooses left/right child pointers based on incoming order prices. Because market orders are distributed across thousands of price levels, the CPU branch predictor experiences frequent branch misses during tree descent and rebalancing rotations.
- **Implementation B (`std::vector`):** Binary search (`std::lower_bound`) splits search spaces in half at each iteration. With uniform price distribution, the comparison condition is essentially random (~50% probability), leading to frequent branch mispredictions and pipeline stalls.
- **Implementation C (`OrderBookDirect`):** Completely branchless price lookup (`idx = tick - 5000`). The only branch is monotonic BBO updating (`if (idx > best_bid_idx_)`). In typical market dynamics where the best bid/ask moves incrementally, this branch is highly predictable by modern branch predictors (e.g. TAGE).

---

### 11.3 Cache Behavior & Memory Locality
- **Implementation A (`std::map`):**
  - Allocates every tree node individually on the heap.
  - Nodes are non-contiguous in memory. Traversing the tree requires pointer chasing across different cache lines, causing severe L1 and L2 Data Cache misses.
- **Implementation B (`std::vector`):**
  - Keeps all levels packed contiguously in memory. When reading levels, the hardware prefetcher pulls subsequent elements into L1 cache lines (64 bytes hold multiple price levels).
  - However, insertion and deletion in the middle require memory shifting (`memmove`), which introduces cache line write traffic when many price levels are active.
- **Implementation C (`OrderBookDirect`):**
  - Pre-allocates a dense array of 10,001 `long` integers: $10,001 \times 8 \text{ bytes} \approx 80 \text{ KB}$.
  - The entire order book fits comfortably inside a standard CPU L2 cache (1.25 MB to 2 MB per core).
  - Top-of-book trading activity repeatedly accesses the same few contiguous cache lines, keeping them perpetually "hot" in the L1 Data Cache.

---

### 11.4 Page Faults & Memory Allocation Behavior
- **Implementation A:** Continuously calls `operator new` and `operator delete` as price levels are added and emptied. Dynamic heap calls incur allocator lock overhead, potential heap fragmentation, and minor page faults when expanding heap bounds.
- **Implementation B:** Allocator calls happen only when resizing the vector buffer, but shifting elements generates memory copies.
- **Implementation C:** Allocates its backing array once up front during construction. Zero allocations occur during subsequent operations, producing **zero steady-state page faults** and deterministic memory usage.

---

### 11.5 Production Choice: Which Implementation Would You Deploy?
If deploying to an institutional high-frequency trading system, **Implementation C (`OrderBookDirect`)** is the unambiguous choice:

1. **Ultra-Low Latency:** Lowest median execution times across all operations (~106 ns per add).
2. **Predictability & Zero Jitter:** Zero dynamic memory allocations during trading hours ensures the complete absence of tail-latency spikes (P99/P99.9) caused by heap allocator locks.
3. **Cache Footprint:** A fixed ~80 KB footprint guarantees the entire price spectrum resides in CPU cache, minimizing memory bus contention.
4. **Maintainability & Simplicity:** The direct indexing logic is straightforward and free of complex tree rebalancing logic.

---

### 11.6 What Would You Try Next?
To achieve sub-50 nanosecond execution in an ultra-low-latency production environment:
1. **Integer Fixed-Point Representation:** Eliminate all floating-point math entirely; pass integer ticks directly from network socket decoders.
2. **Flat Hash Map for Order IDs:** Replace standard `std::unordered_map` with a cache-friendly Robin Hood or Swiss Table flat hash map to drop cancellation lookup times from ~230 ns to ~50 ns.
3. **SIMD-Accelerated Bitmasks:** Maintain bitmasks (e.g., `uint64_t` words) marking which price levels have non-zero quantity, enabling `_tzcnt_u64` (trailing zero count) CPU instructions to locate the next active BBO in a single cycle upon cancellation.
4. **Cache Line Alignment (`alignas(64)`):** Align the BBO state and top-of-book arrays to 64-byte boundaries to eliminate false sharing and split-line accesses.
