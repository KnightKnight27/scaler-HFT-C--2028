# Low-Latency SPSC Queue

**Email:** tejas.24bcs10313@sst.scaler.com
**Roll No:** 24bcs10313

---

## Overview

A Single-Producer Single-Consumer (SPSC) lock-based queue designed for low-latency message passing between two threads. The implementation uses a custom **SpinLock** (busy-wait) for synchronization and a fixed-size ring buffer backed by `std::array` as the underlying storage.

The goal is to measure how many **64-byte objects** can be pushed and popped per second through this queue.

## Design

### SpinLock (`spin_lock.cpp`)

A lightweight mutual exclusion primitive that busy-waits (spins) instead of yielding to the OS scheduler:

- **`lock()`** — Atomically exchanges the flag to `true` using `memory_order_acquire`. Spins in a tight `while` loop until the lock is free.
- **`unlock()`** — Stores `false` with `memory_order_release`, making prior writes visible to the next acquirer.
- Satisfies the C++ *Lockable* requirement, so it works with `std::lock_guard`.

### SPSC Queue (`spsc_queue.cpp`)

A bounded, templated ring-buffer queue:

| Component | Description |
|-----------|-------------|
| `mPushIdx` | Producer write cursor, `alignas(64)` to avoid false sharing |
| `mPopIdx` | Consumer read cursor, `alignas(64)` to avoid false sharing |
| `mStorage` | Fixed-size `std::array<T, Size>` ring buffer |
| `mLock` | SpinLock guarding both push and pop |

- **`push(const T&)`** — Acquires the spin lock, checks if the buffer has space (`mPushIdx - mPopIdx < Size`), writes the item at `mPushIdx % Size`, and increments the push index.
- **`pop(T&)`** — Acquires the spin lock, checks if items are available (`mPushIdx > mPopIdx`), reads the item at `mPopIdx % Size`, and increments the pop index.

Both methods return `true` on success and `false` when the queue is full (push) or empty (pop).

## Build & Run

```bash
# Compile with optimizations
g++ -std=c++17 -O2 -pthread -o spsc_queue spsc_queue.cpp

# Run
./spsc_queue
```

## Throughput Specs

| Metric | Value |
|--------|-------|
| Object size | 64 bytes |
| Synchronization | SpinLock (busy-wait, `std::atomic<bool>`) |
| Queue capacity | Compile-time template parameter |
| Cache-line alignment | `alignas(64)` on both indices to prevent false sharing |

> **Note:** Actual ops/sec figures depend on the hardware and will be printed at runtime by the producer-consumer benchmark harness. To collect your numbers, wrap the push/pop loop in a 1-second window (`std::chrono::steady_clock`) and count successful operations.

## Possible Improvements

- **Lock-free design** — Replace the spin lock with relaxed/acquire-release atomics on the indices (classic Lamport queue), eliminating mutual exclusion entirely.
- **Memory pool** — Pre-allocate a pool of 64-byte blocks to avoid allocator overhead.
- **Cache-line padding** — Pad the storage elements themselves to 64 bytes to reduce cache-line contention.
- **TTAS (Test-and-Test-and-Set)** — Reduce bus traffic on the spin lock by reading the flag before attempting `exchange`.
