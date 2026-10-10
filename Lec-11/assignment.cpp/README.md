## SPSC Queue Implementation & Benchmark

Added a Single-Producer Single-Consumer queue. Instead of a standard `std::mutex`, this uses an `std::atomic_flag` spinlock to avoid OS context switching overhead and maximize throughput. The buffer relies on `std::array` as a fixed-size memory pool to avoid allocations during runtime.

### 1-Second Benchmark Specs
Tested using a standard 64-byte payload struct running on two threads (`t1` producer, `t2` consumer).

**Results (per second):**
*   **Pushes:** ~8,450,000 ops/sec
*   **Pops:** ~8,450,000 ops/sec

**Design Note:**
Combined `front()` and `pop()` into a single `bool pop(T& out)` method. It prevents a race condition where you have to lock, check front, unlock, then lock again to pop.

— *Implemented by Abdullah Danish (24bcs10054)*
