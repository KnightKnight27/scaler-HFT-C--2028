# Low-Latency Order Book: Design, Benchmark & Analysis

**Author:** Nitish Kumar Bhambu | **Roll:** 24BCS10589 | **Email:** nitish.24bcs10589@sst.scaler.com  
**Course:** High-Performance C++ (Capstone Project)

---

## 1. Project Overview

This project implements a high-performance single-instrument resting order book evaluated across three distinct architectural designs in C++17:

1. **Implementation A (Vector-based):** Sorted `std::vector` containers with binary search (`std::lower_bound`).
2. **Implementation B (Map-based):** Node-based red-black trees using `std::map`.
3. **Implementation C (Ring Buffer Slot Pool):** Contiguous preallocated memory pool with a circular queue for constant-time slot recycling and direct array indexing for price levels.

All implementations strictly adhere to price-time priority semantics, tick constraints (0.01 tick size; bids in [50.00, 99.99], asks in [100.01, 150.00]), integer tick representations, and single-threaded execution compiled with `-O2`.

---

## 2. Build and Run

### Quick Build with Makefile
```bash
make clean
make -j$(nproc)
```

### Or Direct Compilation with g++
```bash
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic \
    order_book.cpp map_order_book.cpp ring_order_book.cpp bench.cpp -o book
```

### Or with CMake
```bash
cmake -B build
cmake --build build -j$(nproc)
```

### Running Correctness Tests
```bash
./book --verify
```
Runs comprehensive edge case validations (empty book, duplicate IDs, boundary ticks, overflow, sparse gaps, invalid inputs), ring wraparound/capacity tests, and a 5,000-operation differential fuzzing test against an independent oracle checking full depth after every step.

### Running Benchmarks
```bash
# Quick smoke runs (10,000 operations)
./book --impl A --bench 10000 --repeats 1
./book --impl B --bench 10000 --repeats 1
./book --impl C --bench 10000 --repeats 1

# Full benchmark run (1,000,000 operations, 5 repeats)
./book --impl A --bench 1000000 --repeats 5
./book --impl B --bench 1000000 --repeats 5
./book --impl C --bench 1000000 --repeats 5
```

---

## 3. Architecture & Data Structures

### Implementation A: Flat Sorted Vectors (`order_book.cpp`)
- **Order Storage:** `std::vector<Order>` kept sorted by order ID. Lookups and insertions use binary search via `std::lower_bound`. Cancelled orders have quantity set to zero in-place to avoid expensive $O(N)$ vector element shifting.
- **Price Levels:** Two `std::vector<Level>` (one for bids, one for asks), sorted by price in ascending order.
- **BBO:** Best bid is `bids.back()`, best ask is `asks.front()`. Retrieved in $O(1)$ time.
- **Trade-offs:** Highly cache-friendly sequential memory layout for linear traversals. However, binary search across large vectors causes CPU branch mispredictions and L1 data cache misses, and the vector grows monotonically as cancelled orders retain their slots.

### Implementation B: Ordered Maps (`map_order_book.cpp`)
- **Order Storage:** `std::map<uint64_t, Order>` stores active orders keyed by ID.
- **Price Levels:** `std::map<int, long>` for bids and asks, automatically keeping prices sorted.
- **BBO:** Highest bid is `bids.rbegin()`, lowest ask is `asks.begin()`. Retrieved in $O(1)$ amortized pointer access.
- **Trade-offs:** Node-based red-black trees eliminate contiguous reallocation and easily handle arbitrary ID additions/deletions. However, individual node allocations fragment memory, pointer-chasing degrades cache locality, and tree rebalancing adds CPU overhead.

### Implementation C: Ring-Buffer Slot Pool + Direct Arrays (`ring_order_book.cpp`)
- **Order Storage:** Preallocated contiguous `std::vector<Order>` pool (default capacity: 600,000 simultaneous live orders). An `unordered_map<uint64_t, size_t>` maps order IDs to slot indices.
- **Free Slot Management:** A circular ring buffer (`std::vector<size_t>`) manages free slot indices with `head` and `tail` pointers. Adding an order pops a slot from `head`; cancelling an order returns the slot at `tail`. Slot allocation and deallocation are $O(1)$ without memory reallocation.
- **Price Levels:** Fixed-size direct arrays `std::array<long, 5000>` for bids and asks. Price ticks map directly to array indices (`index = 9999 - price` for bids, `index = price - 10001` for asks).
- **BBO:** Best bid and ask indices are cached. Updating BBO upon addition is $O(1)$. Upon cancellation of the best level, a scan advances to the next non-zero level.
- **Trade-offs:** Fastest execution throughput (~1.8x faster than A, ~4.2x faster than B). Preallocation incurs an initial setup cost, and capacity is bounded by the pool size.

---

## 4. Complexity Analysis

| Operation | Implementation A (Vector) | Implementation B (Map) | Implementation C (Ring Pool + Arrays) |
|---|---|---|---|
| **Add Order** | $O(\log H + \log L)$ append / $O(H + L)$ shift | $O(\log N + \log L)$ | Amortized $O(1)$ |
| **Cancel Order** | $O(\log H + \log L)$ | $O(\log N + \log L)$ | Amortized $O(1)$ avg, $O(P)$ worst |
| **Get BBO** | $O(1)$ | $O(1)$ | $O(1)$ |
| **Get Depth ($d$)** | $O(d)$ | $O(d)$ | $O(P)$ worst, $O(d)$ avg |
| **Memory Overhead** | $O(H + L)$ (retains ID history) | $O(N + L)$ (node pointers) | $O(K + N + P)$ (preallocated pool) |

*Definitions:*
- $H$: Total historical orders added
- $N$: Current live orders
- $L$: Occupied price levels
- $K$: Pool capacity (600,000)
- $P$: Total possible price levels per side (5,000)
- $d$: Requested depth count

---

## 5. Interface Changes

- **Integer Tick Prices:** Prices are represented as `int` ticks (where 1 tick = 0.01 currency units) instead of floating-point `double` to eliminate rounding errors and speed up CPU comparisons.
  - Bid range `[50.00, 99.99]` $\rightarrow$ `[5000, 9999]`
  - Ask range `[100.01, 150.00]` $\rightarrow$ `[10001, 15000]`
- **Bounded Pool Configuration (Impl C):** `RingOrderBook(size_t capacity = 600000)` allows configuring the maximum live order pool at construction. Exceeding capacity safely throws `std::length_error` without corrupting state.

---

## 6. Verification and Benchmarking Suite

The harness in `bench.cpp` performs:
1. **Unit & Edge-Case Checks:** Tests empty books, quantity aggregation, non-positive depths, unknown cancellations, boundary price ticks, duplicate live IDs, and integer overflow protection.
2. **Ring Buffer Invariants:** Tests small-capacity pools (capacities 1, 2, 7), wraparound mechanics, rejection without slot consumption, and complete drain cycles.
3. **Differential Replay:** Replays 5,000 operations across all three engines simultaneously, verifying exact BBO and 5,000-level depth equality after every single operation.
4. **Benchmark Timing:** Measures end-to-end replay, per-operation latency (add, cancel, BBO query), and lifecycle times across 1,000,000 operations with fixed seed 42 (70% adds, 20% cancels, 10% BBO queries, depth-10 snapshots every 100 ops).

Detailed Linux `perf` profiling, memory analysis, hardware performance counter tables, and bottleneck investigations are available in [REPORT.md](REPORT.md) and [output/pdf/performance_report.pdf](output/pdf/performance_report.pdf).
