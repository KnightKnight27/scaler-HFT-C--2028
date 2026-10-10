# SPSC Queue Benchmark (64-byte Objects)

## Student Details
- **email**: "24bcs10464@sst.scaler.com"
- **roll_no**: "24BCS10464"
- **Name**: Krushna Sonawane
- **GitHub**: [AGENT-BABA](https://github.com/AGENT-BABA)

---

## Overview

This assignment implements a thread-safe, lock-based **Single-Producer Single-Consumer (SPSC)** circular buffer queue supporting 64-byte objects (`alignas(64)`).

Two lock mechanisms were evaluated:
1. **SpinLock**: Implemented using `std::atomic_flag` with acquire-release semantics and CPU pause instruction (`_mm_pause()` / `__builtin_ia32_pause()`).
2. **`std::mutex`**: Standard OS-level mutex using `std::unique_lock`.

The benchmark executes two dedicated threads (`t1` as producer and `t2` as consumer) running concurrently for a 1-second timed window, joined cleanly via `t1.join()` and `t2.join()`.

---

## Hardware & Environment

- **CPU**: 13th Gen Intel(R) Core(TM) i5-13450HX (10 Cores: 6P + 4E, 16 Logical Processors)
- **Compiler**: g++ 16.2.0 (MSYS2) with `-O3 -std=c++17 -pthread`
- **OS**: Windows 11 (x86_64)
- **Object Size**: 64 bytes (`alignas(64) Object64`)
- **Queue Capacity**: 4096 elements (circular ring buffer)

---

## Benchmark Results (1 Second Execution)

| Metric | SpinLock (`std::atomic_flag`) | `std::mutex` |
| :--- | :--- | :--- |
| **Duration** | ~1.0026 s | ~1.0044 s |
| **Successful Pushes** | **13,528,527** (13.49M ops/s) | **11,728,701** (11.68M ops/s) |
| **Successful Pops** | **13,524,432** (13.49M ops/s) | **11,728,700** (11.68M ops/s) |
| **Total Throughput** | **27,052,959 ops** (**26.98M ops/s**) | **23,457,401 ops** (**23.36M ops/s**) |

---

## Observations & Analysis

1. **SpinLock Advantage**:
   - The critical section in this SPSC queue is extremely tight: copying one 64-byte object and updating an index.
   - The user-space spinlock avoids OS kernel scheduling and context switches, resulting in **~15% higher throughput** (~27.0M ops/s vs ~23.4M ops/s).
2. **Producer-Consumer Balance**:
   - Both the producer and consumer remained in near-lockstep throughput (~13.5M pushes vs ~13.5M pops for SpinLock), ensuring minimum queue starvation or overrun.

---

## Build & Run

```bash
# Compile with -O3 optimizations
g++ -O3 -std=c++17 -pthread Lec-11/assignment.cpp/spsc_queue.cpp -o spsc_bench

# Run the benchmark
./spsc_bench
```
