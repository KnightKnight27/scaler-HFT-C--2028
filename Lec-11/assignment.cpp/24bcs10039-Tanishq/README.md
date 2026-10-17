# High-Performance Single Producer Single Consumer (SPSC) Queue

An ultra-low latency, high-throughput Single Producer Single Consumer (SPSC) Ring Buffer queue in modern C++17, designed for High-Frequency Trading (HFT) and low-latency systems.

This project investigates the latency, throughput, and hardware-level performance implications across 6 queue architectures, analyzing **cache line bouncing**, **false sharing**, **OS context switches**, and **memory ordering semantics**.

---

## Hardware & System Specifications

- **Device**: Apple MacBook
- **CPU**: Apple M2 (8 Cores: 4 Performance + 4 Efficiency cores, ARMv8.5-A / arm64)
- **OS**: macOS Darwin 25.6.0
- **Compiler**: Apple Clang 21.0.0 (`-std=c++17 -O3 -march=native -pthread`)
- **L1 Data Cache**: 128 KB per performance core (128-byte cache line)
- **Tested Payload**: 64-byte `Order` struct (`sizeof(Order) == 64`, cache line aligned)
- **Ring Buffer Capacity**: 65,536 elements ($2^{16}$, power of two)

```cpp
struct Order {
    uint64_t order_id;
    uint64_t timestamp_ns;
    double price;
    uint32_t quantity;
    char symbol[16];
    char side;          // 'B' or 'S'
    char order_type;    // 'L' (Limit) or 'M' (Market)
    char padding[14];   // Explicit pad to exactly 64 bytes
};
static_assert(sizeof(Order) == 64, "Order struct must be exactly 64 bytes");
```

---

## 1-Second Benchmark Results

Each configuration is evaluated for **1 full second** under continuous, full-tilt production and consumption across two dedicated threads:

| Architecture / Queue Strategy | Throughput (Mops/s) | Throughput (ops/sec) | Bandwidth (MB/s) | Bandwidth (GB/s) | Avg Latency (ns/op) | Involuntary Ctx Switches | User / Sys CPU (s) | Relative Speedup |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Lock-Free (`alignas(64)`)** | **30.68** | **30,675,294** | **1,872.27** | **1.83** | **32.60** | **674** | 2.05s / 0.05s | **1.63x** |
| **Lock-Free (Cached Indices)** | **25.58** | **25,584,605** | **1,561.56** | **1.52** | **39.09** | **713** | 2.05s / 0.06s | **1.36x** |
| **Lock-Free (False Sharing Bug)** | **19.15** | **19,148,599** | **1,168.74** | **1.14** | **52.22** | **643** | 2.05s / 0.05s | **1.02x** |
| **`std::mutex`** | **18.84** | **18,839,536** | **1,149.87** | **1.12** | **53.08** | **254,670** | 0.82s / 0.88s | **1.00x** (base) |
| **Naive SpinLock (TAS exchange)** | **5.42** | **5,416,969** | **330.63** | **0.32** | **184.61** | **1,357** | 1.95s / 0.06s | **0.29x** |
| **TTAS SpinLock (Spin on load)** | **3.85** | **3,852,683** | **235.15** | **0.23** | **259.56** | **859** | 2.05s / 0.06s | **0.20x** |

---

## Architectural & Low-Latency Insights

### 1. Power-of-Two Ring Buffer Indexing
Instead of costly division and modulo (`%`) instructions (which can cost 10–30 CPU cycles), the queue capacity is constrained to a power of two ($2^k$):
$$\text{slot} = \text{index} \ \& \ (\text{Capacity} - 1)$$
This translates to a single 1-cycle bitwise `AND` instruction (`kMask`).

### 2. False Sharing Elimination (`alignas(64)`)
- **The Bug**: When `tail_` and `head_` are declared adjacent, they reside on the **same 64-byte cache line**. Even though Producer only writes to `tail_` and Consumer only writes to `head_`, every write by one core invalidates the entire cache line in the other core's L1 cache (MESI HITM / ping-pong).
- **The Fix**: Aligning both indices with `alignas(64)` places `tail_` and `head_` on distinct cache lines.
- **The Result**: Throughput jumps from **19.15 Mops/s** to **30.68 Mops/s** — a **+60% performance boost** purely from cache layout!

### 3. Context Switches: `std::mutex` vs SpinLocks vs Lock-Free
Using `getrusage(RUSAGE_SELF, ...)` reveals that `std::mutex` triggered **254,670 involuntary context switches** and spent **0.88 seconds in kernel space (`sys CPU`)**. Under high-frequency contention, `std::mutex` falls back to kernel futex calls, putting threads to sleep and context switching.
In contrast:
- Spinlocks and Lock-free queues execute entirely in user space (only ~0.05s sys CPU and <1,000 context switches total).
- The threads stay pinned and hot in CPU registers.

### 4. Cache Line Bouncing: TAS vs TTAS SpinLock
- In the naive **Test-and-Set (TAS)** spinlock (`flag_.exchange(true)`), spinning threads issue atomic Read-Modify-Write (RMW) instructions on every spin cycle, requesting exclusive Modified (`M`) cache state and flooding the interconnect bus with invalidations.
- In **Test-and-Test-and-Set (TTAS)**, spinning threads first read `flag_.load(relaxed)` in Shared (`S`) state without issuing bus invalidations, only attempting an atomic exchange when the lock becomes free.

### 5. Memory Ordering Semantics (`Acquire` / `Release`)
- **Producer**: Writes payload to buffer, then publishes `tail_` using `std::memory_order_release`. This guarantees that memory writes to the `Order` payload cannot be reordered after the store to `tail_`.
- **Consumer**: Reads `tail_` using `std::memory_order_acquire`. This guarantees that subsequent reads from the payload cannot be reordered before the read of `tail_`.

---

## Project Structure

```
spsc_queue/
├── Makefile            # Build recipes for tests and benchmarks
├── README.md           # Comprehensive performance analysis and specs
├── benchmark.cpp       # 1-second benchmark runner with getrusage metrics
├── main.cpp            # Functional verification driver
└── spsc_queue.hpp      # Templated implementations of all SPSC variants:
                        #  - SPSCQueue<T, Lock, Capacity>
                        #  - SpinLock (TAS)
                        #  - TTASSpinLock (Test-and-Test-and-Set)
                        #  - LockFreeSPSCQueueNaive (False sharing bug)
                        #  - LockFreeSPSCQueue (alignas(64) optimized)
                        #  - LockFreeSPSCQueueCached (Shadow cached indices)
```

---

## How to Build and Run

### 1. Compile and Run Comprehensive Benchmark
```bash
cd spsc_queue
make run
```

### 2. Compile Functional Test
```bash
make test_queue
./test_queue
```

### 3. Clean Binaries
```bash
make clean
```
