# SPSC Queue Implementation & Benchmarks

**Name:** Tirth Bhalani  
**Roll No:** `24BCS10098`  
**Email:** `tirth.24bcs10098@sst.scaler.com`  

---

## Overview
This repository implements a Single Producer Single Consumer (SPSC) queue designed for ultra-low latency C++ applications and High-Frequency Trading (HFT). It benchmarks pushing and popping 64-byte aligned objects across 10,000,000 operations between two concurrent threads (`t1` producer, `t2` consumer).

---

## Performance Statistics (Hardware Output)

Below are the per-second specs and execution times captured across the implemented strategies (**SpinLock**, **std::mutex**, and **Atomic Lock-Free**):

```bash
$ ./spsc spin
SpinLock Queue: 1.65883 s, 6028360 objects/sec

$ ./spsc mutex
std::mutex Queue: 0.822564 s, 12157104 objects/sec

$ ./spsc atomic
Atomic Lock-Free Queue: 0.0978526 s, 102194525 objects/sec
```

---

## Detailed Performance Analysis & Comparison

| Metric / Strategy | SpinLock Queue (`./spsc spin`) | std::mutex Queue (`./spsc mutex`) | Lock-Free Atomic Queue (`./spsc atomic`) | Difference / Optimization |
| :--- | :---: | :---: | :---: | :--- |
| **Throughput (64B ops/sec)** | **~6.02 Million/sec** | **~12.15 Million/sec** | **~102.19 Million/sec** | **8.4x to 16.9x Higher Throughput** |
| **Execution Time (10M Ops)** | `1.658 s` | `0.822 s` | `0.097 s` | **~94% Reduction in Total Time** |
| **Synchronization** | CAS Spinlock (`compare_exchange`) | OS Mutex Lock (`lock_guard`) | Lock-Free Atomic Ring Buffer | Zero locking overhead |
| **Cache Line Alignment** | Shared | Shared | `alignas(64)` for `head` & `tail` | Eliminates False Sharing |

---

## Key Hardware Insights

1. **Cache Line Isolation (`alignas(64)`)**:
   - `head` (consumer index) and `tail` (producer index) are isolated on separate 64-byte cache lines.
   - This eliminates **false sharing**, ensuring L1/L2 cache invalidation does not occur between core threads when producer or consumer updates indices.

2. **Acquire-Release Memory Ordering**:
   - Uses `memory_order_release` when publishing items and `memory_order_acquire` when consuming items.
   - Avoids expensive default `memory_order_seq_cst` full memory fence overhead.

3. **Lock Elimination & CPU Spin Efficiency**:
   - SpinLock and `std::mutex` introduce significant CPU contention and thread locking overhead.
   - The atomic lock-free queue eliminates locking entirely, achieving **102+ Million 64-byte operations per second**.

---

## Environment & Timing Note

- Runtime execution speed and per-second throughput are measured using high-precision `std::chrono::steady_clock` inside `spsc_queue.cpp`.

---

## Build & Execution Instructions

### Compilation
```bash
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc
```

### Running Benchmarks
- **Run All Implementations**:
  ```bash
  ./spsc
  ```

- **Run Specific Strategy**:
  ```bash
  ./spsc spin      # SpinLock implementation
  ./spsc mutex     # std::mutex implementation
  ./spsc atomic    # Atomic lock-free implementation
  ```
