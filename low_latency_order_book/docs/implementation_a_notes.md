# Implementation A: Sorted Map & Hash Map (Baseline)

## 1. Overview & Data Structures Used
Implementation A serves as our intuitive baseline design. It prioritizes correctness, ease of implementation, and standard library familiarity over hardware optimization.
- **Per-Order Storage:** `std::unordered_map<uint64_t, OrderDetails>` providing $O(1)$ average-time order lookup for fast cancellations.
- **Per-Price Storage:** 
  - `std::map<int, long, std::greater<int>> bids_` (Sorted descending by tick price so `.begin()` yields the Best Bid).
  - `std::map<int, long, std::less<int>> asks_` (Sorted ascending by tick price so `.begin()` yields the Best Ask).

---

## 2. Algorithmic Complexity
| Operation | Complexity | Explanation |
| :--- | :--- | :--- |
| `add_order` | $O(\log P + 1)$ | Hash map insertion is $O(1)$ average; `std::map` insertion/update takes $O(\log P)$ where $P$ is the number of active price levels. |
| `cancel_order` | $O(\log P + 1)$ | $O(1)$ lookup in `unordered_map`, followed by $O(\log P)$ tree traversal to update or erase the price level. |
| `get_bbo` | $O(1)$ | Because `std::map` keeps keys sorted, `.begin()` points directly to the Best Bid or Best Ask. |
| `get_bids` / `get_asks` | $O(D)$ | Traverses $D$ depth levels sequentially from the tree iterator. |

---

## 3. How to Build and Run
To compile and test Implementation A from the project directory:

```bash
# Compile with -O2 optimization
g++ -O2 -std=c++17 src/main.cpp src/order_book_a.cpp -Iinclude -o bench.exe

# Run Correctness Verification
./bench.exe --verify

# Run Benchmark (1,000,000 operations, 5 runs median)
./bench.exe --bench
```

---

## 4. Empirical Benchmark Results (Median of 5 Runs - Linux/WSL)
- **Total Execution Time:** `165.403 ms`
- **Avg Latency per `add_order`:** `138.955 ns`
- **Avg Latency per `cancel_order`:** `234.318 ns`
- **Avg Latency per `get_bbo`:** `20.349 ns`

**Linux `perf stat` Metrics (5 runs):**
- **Task-Clock:** `943.79 msec` (±1.45%)
- **Page Faults:** `22,774` (allocator churn from node insertions)
- *Note on PMU Events (cycles, instructions, cache misses): Virtualized PMU is disabled by default in WSL2, producing `<not supported>` for hardware counters.*

**Final State Verification:**
- **Best Bid:** `99.99 x 2722`
- **Best Ask:** `100.01 x 2525`
- **Checksum:** `25247`

---

## 5. C++ Concepts & Hardware Behavior Analysis
- **Dynamic Heap Allocation:** Every node inserted into `std::map` and `std::unordered_map` allocates memory dynamically on the heap (`new`/`malloc`), introducing allocator overhead and heap fragmentation over time.
- **Pointer Chasing & Cache Inefficiency:** Red-Black trees store nodes as scattered heap elements connected by pointers. Traversing them requires jumping across arbitrary memory addresses, resulting in frequent **L1/L2 data cache misses**.
- **Branch Prediction Overhead:** Tree rebalancing logic (`std::map` insertions and erasures) involves complex conditional branching, which increases **branch mispredictions** during random workloads.

---

## 6. Key Takeaways
While Implementation A is robust and easy to reason about, its reliance on node-based heap structures makes it ill-suited for ultra-low-latency trading environments where minimizing cache misses and avoiding heap allocations is critical.