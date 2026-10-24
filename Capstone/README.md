
# Capstone: Low-Latency Order Book in C++17

**Author:** Rachit S   
**SST Email:** rachit.24bcs10139@sst.scaler.com  
**Roll Number:** 24bcs10139   
**Course:** Scaler HFT C++ (2028 Batch)  

---

## 1. Overview
This project implements, verifies, benchmarks, and profiles a single-instrument resting limit order book across three genuinely distinct data-structure architectures:
- **Implementation A (`OrderBookA`):** Red-Black Tree (`std::map`) for price levels + Chained Hash Table (`std::unordered_map`) for order cancellation.
- **Implementation B (`OrderBookB`):** Flat Contiguous Sorted Dynamic Arrays (`std::vector`) with Binary Search (`std::lower_bound`) for both price levels and order lookup.
- **Implementation C (`OrderBookC`):** Direct Tick-Indexed Fixed Arrays (`std::array<long, 5000>`) with cache-line-aligned (`alignas(64)`) cached BBO indices and a pre-allocated direct-index table for $O(1)$ order lookup.

Full hardware profiling data and the 1–2 page performance analysis report can be found in [`PERFORMANCE_ANALYSIS.md`](./PERFORMANCE_ANALYSIS.md).

---

## 2. How to Build the Project
The project uses only the C++17 standard library with zero external dependencies. Compile the unified verification and benchmark executable with `-O2`:

```bash
g++ -O2 -std=c++17 bench.cpp -o bench

```

---

## 3. How to Run

### 3.1 Run the Correctness Test (`--verify`)

Proves that all three implementations produce identical state transitions across edge cases (empty book, cancelling non-existent/already-cancelled orders, depth `< 10`) and a 50,000-operation randomized lockstep workload:

```bash
./bench --verify

```

**Correctness Output:**

```text
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.

```

### 3.2 Run the Benchmarks (`--impl A|B|C`)

Executes the 1,000,000-operation repeatable workload (`std::mt19937 rng(42)`, 70% Add, 20% Cancel, 10% BBO, plus `get_bids(10)` and `get_asks(10)` every 100 operations) for 5 runs and reports the median latencies and final checksum:

```bash
./bench --impl A
./bench --impl B
./bench --impl C

```

### 3.3 Run Hardware Profiling with Linux `perf`

```bash
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl B

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl C

```

---

## 4. Interface & Price Representation Documentation

* **Public API:** Preserves the exact required interface (`add_order`, `cancel_order`, `get_bbo`, `get_bids`, `get_asks`) without modification.
* **Compile-Time Dispatch:** Rather than using `virtual` methods (which introduce `vtable` pointer indirection and prevent inlining), `bench.cpp` uses a C++ template (`template <typename OrderBookImpl>`) to benchmark all three implementations under the same logical interface with zero runtime dispatch overhead.
* **Internal Integer Ticks:** Because decimal fractions like `0.01` cannot be represented exactly in binary floating-point (`double`), all incoming prices are converted at the API boundary into exact 32-bit integer ticks (`int32_t`) via `std::llround(price * 100.0)`:
* Bid ticks: `[50.00, 99.99]` $\rightarrow$ `[5000, 9999]` (`5,000` ticks).
* Ask ticks: `[100.01, 150.00]` $\rightarrow$ `[10001, 15000]` (`5,000` ticks).



---

## 5. Data-Structure Choices & Big-O Complexity (Section 4 Required Analysis)

Let $M$ be the number of active price levels ($M \le 5,000$), $T$ be the total possible ticks per side ($T = 5,000$), $N$ be the number of stored orders ($N \le 1,000,000$), and $D$ be the requested depth ($D = 10$).

### 5.1 Implementation A (`OrderBookA`)

1. **Data structures used:** `std::map<Tick, long, std::greater<Tick>>` (bids), `std::map<Tick, long>` (asks), and `std::unordered_map<uint64_t, StoredOrder>` (orders).
2. **How orders are stored:** Stored as `{side, tick, qty}` nodes inside heap-allocated hash table buckets (`std::unordered_map`).
3. **How price levels are stored:** Stored as heap-allocated Red-Black Tree nodes containing parent/left/right pointers, color bits, tick key, and aggregated quantity.
4. **How the best bid/ask is found:** Dereferencing `.begin()` on both `std::map` trees ($O(1)$ via the tree's leftmost node pointer).
5. **How an order is found during cancellation:** $O(1)$ average hash lookup in `orders.find(id)`, followed by an $O(\log M)$ tree lookup in `bids` or `asks` to subtract quantity and `.erase()` the tree node if quantity hits `0`.

### 5.2 Implementation B (`OrderBookB`)

1. **Data structures used:** Three contiguous `std::vector` buffers (`bids`, `asks`, and `orders`) pre-allocated via `.reserve()`.
2. **How orders are stored:** Appended in strictly increasing `id` order into a contiguous `std::vector<OrderEntry>` (`{id, side, tick, qty, active}`).
3. **How price levels are stored:** Stored in two contiguous sorted vectors containing only active levels (`qty > 0`). `bids` is sorted **ascending** (best bid at `bids.back()`) and `asks` is sorted **descending** (best ask at `asks.back()`) so top-of-book insertions and deletions shift minimal elements.
4. **How the best bid/ask is found:** Direct $O(1)$ access via `bids.back()` and `asks.back()`.
5. **How an order is found during cancellation:** Binary search (`std::lower_bound`) over `orders` by `id` in $O(\log N)$ time, marking `active = false`, followed by binary search (`std::lower_bound`) over `bids`/`asks` in $O(\log M)$ time (and $O(M)$ contiguous element shift if a level drops to `0`).

### 5.3 Implementation C (`OrderBookC`)

1. **Data structures used:** Fixed-size `std::array<long, 5000>` for `bid_qty` and `ask_qty`, cache-line-aligned (`alignas(64)`) `best_bid_idx` and `best_ask_idx` cursors, and a pre-allocated direct-indexed `std::vector<DirectOrderSlot> order_table`.
2. **How orders are stored:** Indexed directly by `id` (`order_table[id] = {qty, tick, side, true}`) in a single contiguous pre-allocated array.
3. **How price levels are stored:** Indexed directly by tick offset (`bid_qty[tick - 5000]` and `ask_qty[tick - 10001]`), requiring zero dynamic memory allocations and zero pointer chasing.
4. **How the best bid/ask is found:** Direct $O(1)$ read of cached `best_bid_idx` and `best_ask_idx` (updated in $O(1)$ on `add_order`).
5. **How an order is found during cancellation:** Direct $O(1)$ array lookup at `order_table[id]`, flipping `active = false` and decrementing `bid_qty[idx]` or `ask_qty[idx]`. Only if the best level drops to `0` does the index scan downward/upward to the next non-zero slot.

### 5.4 Big-O Complexity Comparison Table

| Operation | Implementation A (`OrderBookA`) | Implementation B (`OrderBookB`) | Implementation C (`OrderBookC`) |
| --- | --- | --- | --- |
| **`add_order`** | $O(\log M)$ avg | $O(\log M + M)$ | **$O(1)$** |
| **`cancel_order`** | $O(\log M)$ avg | $O(\log N + \log M + M)$ | **$O(1)$ amortized** ($O(T)$ worst-case if BBO emptied) |
| **`get_bbo`** | $O(1)$ | $O(1)$ | **$O(1)$** |
| **`get_bids(D)`** | $O(D)$ pointer hops | $O(D)$ contiguous scan | **$O(D + S)$** contiguous scan ($S$ = empty ticks skipped) |
| **`get_asks(D)`** | $O(D)$ pointer hops | $O(D)$ contiguous scan | **$O(D + S)$** contiguous scan ($S$ = empty ticks skipped) |

---

## 6. Summary Results Table

| Metric | Implementation A (`std::map` + `unordered_map`) | Implementation B (Sorted `std::vector` + Binary Search) | Implementation C (Direct Array + Cached BBO) |
| --- | --- | --- | --- |
| **Total time (ms)** | `320.71 ms` | `250.24 ms` | `75.45 ms` |
| **ns / add** | `276.60 ns` | `139.39 ns` | `33.46 ns` |
| **ns / cancel** | `465.27 ns` | `584.75 ns` | `114.40 ns` |
| **ns / BBO** | `22.84 ns` | `22.47 ns` | `21.98 ns` |
| **IPC** | `0.62` | `0.69` | `1.18` |
| **Branch misses (%)** | `5.45%` | `9.50%` | `1.66%` |
| **L1-dcache-load-misses** | `97,787,164` | `172,260,505` | `11,498,638` |
| **Cache misses** | `52,443,406` | `103,577,688` | `12,061,978` |
| **dTLB-load-misses** | `4,411,197` | `6,355,000` | `430,164` |
| **Page faults** | `19,699` | `21,889` | `22,622` |

```

