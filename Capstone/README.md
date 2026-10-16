# Capstone: Low-Latency Order Book

**Course:** Low Latency C++ / HFT  
**Team Members:**  
- Name: Parth Taneja (Roll No: 24BCS10230, Email: parth.24bcs10230@sst.scaler.com)  
- Name: Akshat Sipany (Roll No: 24BCS10059, Email: akshat.24bcs10059@sst.scaler.com)  

---

## 1. Overview & Data Structure Architecture

This project implements a resting limit order book for a single instrument across three genuinely different data-structure designs, benchmarks them on a 1,000,000 operation workload, and verifies correctness.

### Implementation A: `MapOrderBook` (`map_order_bk.hpp`)
- **Underlying Container:** `std::map<int, PriceLevel, std::greater<int>>` for bids (descending) and `std::map<int, PriceLevel>` for asks (ascending).
- **Order & Level Storage:** Each price level maintains an `std::list<Order>` for FIFO queueing. An `std::unordered_map<uint64_t, OrderRef>` stores an iterator pointing directly into the linked list.
- **BBO Lookup:** Returns `bids_.begin()` and `asks_.begin()` in $O(1)$ amortized time.
- **Cancellation:** $O(1)$ hash map lookup to find the order, then an $O(\log L)$ `map::find` to reach its price level, then an $O(1)$ list node erase using the stored iterator.

### Implementation B: `HeapOrderBook` (`pq_order_bk.hpp`)
- **Underlying Container:** `std::priority_queue<int>` for bid prices (max-heap) and `std::priority_queue<int, vector<int>, greater<int>>` for ask prices (min-heap).
- **Order & Level Storage:** `std::unordered_map<int, long>` stores accumulated quantity per price level. Orders are tracked in `std::map<uint64_t, HeapOrder>`.
- **BBO Lookup:** Reads `heap.top()` in $O(1)$ time.
- **Cancellation:** Lazy deletion; when an order is cancelled, level quantity is decremented. Stale price levels are popped from the heap when they reach the top.

### Implementation C: `FixedOrderBook` (`fixed_order_bk.hpp`), Custom Optimized Design
- **Underlying Container:** A single contiguous `std::array<BookLevel, 10000>` covering all 10,000 possible ticks (`[50.00, 99.99]` bids, `[100.01, 150.00]` asks).
- **Contiguous Cache-Friendly Layout:**
  - Bids occupy indices `0..4999` (ticks 5000..9999), sorted in **increasing price towards the center**. The best possible bid sits at index `4999`.
  - Asks occupy indices `5000..9999` (ticks 10001..15000), sorted in **increasing price away from the center**. The best possible ask sits at index `5000`.
  - **Cache Locality:** Best Bid (near index 4999) and Best Ask (near index 5000) meet right in the middle of the array, so the top of book sits in neighbouring memory. With 16-byte levels they usually end up in adjacent 64-byte cache lines rather than the same one (see REPORT.md, section 3.6).
- **Lookup & Cancellation:** Direct array index mapping with `get_idx()`. Cancellations look up the order in `std::unordered_map<uint64_t, OrderInfo>`, decrement level quantity, and scan linearly downwards/upwards only if the best price level becomes empty.
- **No allocation for price levels:** the level array is allocated once. Each new order still allocates one node in the order hash map (perf shows about 19% of C's cycles in the allocator, see REPORT.md).

---

## 2. Price Representation

Prices are stored and processed as fixed-point integer ticks (`int`) with a tick size of `0.01`:
- `$50.00` $\rightarrow$ `5000`
- `$99.99` $\rightarrow$ `9999`
- `$100.01` $\rightarrow$ `10001`
- `$150.00` $\rightarrow$ `15000`

Prices are converted back to decimal strings (`xx.yy`) only when displaying to stdout in the benchmark and test outputs.

---

## 3. Interface Changes

We kept the five operations from the assignment, but changed how prices go in and out. The tick size is fixed at 0.01, so we work with whole numbers of ticks instead of doubles. That way prices compare exactly, and all three books agree on the same price without any rounding surprises.

Here is what is different from the API in the assignment:

| | Assignment | Ours |
| :--- | :--- | :--- |
| `add_order` | `add_order(uint64_t id, Side side, double price, long qty)` | `add_order(uint64_t id, Side side, int price, long qty)` |
| `Level::price` | `double` | `int` (ticks) |
| `BBO::bid_price`, `BBO::ask_price` | `double` | `int` (ticks) |

So a price of 98.56 is passed in as `9856`, and `get_bbo()` hands back `9856` as well. We only turn it back into `98.56` when printing (see `format_price()` in `bench.cpp`).

A few behaviours :

- `add_order` quietly ignores an order if the quantity is zero or negative, if the price is outside its side's band (bids 5000 to 9999, asks 10001 to 15000), or if the order ID is already in the book.
- `cancel_order` returns `false` for an unknown or already cancelled ID and leaves the book untouched.
- `get_bbo()` on an empty side returns `has_bid` / `has_ask` as `false`, with the price and quantity for that side set to 0.
- `get_bids(depth)` and `get_asks(depth)` return however many levels exist if there are fewer than `depth`, and an empty vector if `depth` is 0 or less.

The shared types (`Side`, `Level`, `BBO`) and the price band constants live in `common.hpp`, so all three books use exactly the same definitions.

---

## 4. How to Build

The project requires a C++17 compliant compiler and compiles with `-O2`:

```bash
make
```

To clean the build:
```bash
make clean
```

---

## 5. How to Run

### 5.1 Correctness Verification (`--verify`)
Runs the identical 1,000,000 operation workload across all three implementations, verifying BBO, price levels, quantities, and cancellations at every step:

```bash
./bench --verify
```

Expected output:
```
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

### 5.2 Benchmarking Individual Implementations
Runs 5 benchmark iterations using `std::chrono::steady_clock` and reports the median:

```bash
# Implementation A (MapOrderBook)
./bench --impl A

# Implementation B (HeapOrderBook)
./bench --impl B

# Implementation C (FixedOrderBook)
./bench --impl C
```

---

## 6. Linux `perf` Profiling (for Linux/Lab machines)

To profile hardware counters with Linux `perf`:

```bash
# 1. Performance counter statistics (run 5 times for median)
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl B

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl C

# 2. Call-graph recording
perf record -g ./bench --impl C
perf report
```