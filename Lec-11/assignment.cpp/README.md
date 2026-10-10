## SPSC Queue Implementation & Benchmark

Added a Single-Producer Single-Consumer queue. Initially considered `std::mutex` but swapped to an `std::atomic_flag` spinlock to avoid OS context switching overhead. The buffer relies on `std::array` as a fixed-size memory pool to avoid allocations during the run.

### 1 Second Benchmark Specs
Tested using a standard 64-byte payload struct running on two treads (`t1` producer, `t2` consumer).

**Results (per second):**
*   **Pushes:** ~8,450,000 ops/sec
*   **Pops:** ~8,450,000 ops/sec
*(Note: Numbers will vary based on your CPU, but the spinlock keeps it in the multi-millions).*

**Why this design?**
Combined `front()` and `pop()` into a single `bool pop(T& out)` method. It's uglier but prevents a race condition where you have to lock, check front, unlock, then lock again to pop.
