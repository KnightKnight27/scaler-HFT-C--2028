# HFT C++ Assignment: SPSC Queue 1-Second Benchmark

## Student Details
- **Email:** `angel.24bcs10011@sst.scaler.com`
- **Roll No:** `10011`

---

## Overview
This assignment evaluates how many **64-byte objects** can be pushed and popped in **1 second** using an SPSC (Single Producer Single Consumer) queue under different synchronization mechanisms:
1. **`std::mutex` Queue:** Standard OS-level mutex locking.
2. **Atomic Spinlock Queue:** User-space spinlock using `std::atomic_flag` with `_mm_pause()`.
3. **Lock-Free Atomic Queue:** Ring buffer utilizing atomic indices (`std::memory_order_release` / `std::memory_order_acquire`) and `alignas(64)` to eliminate false sharing between producer and consumer cache lines.

Each queue uses a contiguous pre-allocated buffer (power-of-2 capacity) to eliminate runtime dynamic heap allocations.

---

## Hardware & Environment Specs
- **CPU:** 13th Gen Intel(R) Core(TM) i5-13500H (12 Cores, 16 Threads, base ~2.6 GHz, boost ~4.7 GHz)
- **RAM:** 16 GB DDR5
- **OS:** Windows 11 x86_64
- **Compiler:** `g++ (Rev3, Built by MSYS2 project) 13.2.0`
- **Flags:** `-O3 -std=c++17 -march=native -pthread`
- **Payload Size:** 64 bytes (`alignas(64) struct Object64 { uint64_t data[8]; }`)

---

## Benchmark Results (1.0 Second Execution)

| Implementation | 64B Objects Pushed / sec | 64B Objects Popped / sec | Throughput (Mops/s) | Bandwidth (MB/s) |
|---|---|---|---|---|
| **1. `std::mutex` SPSC** | ~12,850,000 | ~12,800,000 | ~12.8 Mops/s | ~780 MB/s |
| **2. Atomic Spinlock SPSC** | ~10,500,000 | ~10,480,000 | ~10.5 Mops/s | ~638 MB/s |
| **3. Lock-Free Atomic SPSC (`alignas 64`)** | ~33,700,000 | ~33,700,000 | **~33.5 Mops/s** | **~2,036 MB/s** |

---

## Observations & Analysis
1. **Lock Overhead (`std::mutex`):** 
   - Protects both head and tail with a single mutex.
   - Producer and consumer continuously contend for the mutex, leading to lock contention and kernel synchronization overhead (~12.8 Mops/s).
2. **Spinlock Contention:**
   - Atomic spinlock avoids kernel transitions but burns CPU cycles polling `flag.test_and_set()`.
   - Both cores invalidate each other's cache lines on every lock acquisition (MESI ping-pong), resulting in lower throughput (~10.5 Mops/s).
3. **Lock-Free with Cache-Line Separation:**
   - Decoupled `tail_` (written only by producer) and `head_` (written only by consumer) aligned to separate 64-byte cache lines (`alignas(64)`).
   - Uses release-acquire memory order to safely publish slot writes without locks.
   - Eliminates false sharing and lock contention, achieving over **33.5 Million 64-byte operations per second (~2.0 GB/s)**.

---

## Build and Run

```bash
# Compile with optimizations
g++ -O3 -std=c++17 -march=native -pthread Lec-11/assignment.cpp/spsc_queue.cpp -o spsc_bench

# Run the 1-second benchmark
./spsc_bench
```
