# SPSC Queue with SpinLock

**Name:** Parv Mehta  
**Email:** parv.24bcs10301@sst.scaler.com  
**Roll No:** 24bcs10301

## What it does

A single-producer single-consumer ring buffer (`spsc_queue.cpp`) guarded by a busy-wait `SpinLock` (`spin_lock.cpp`). Both indices are `alignas(64)` to avoid false sharing. `push` and `pop` return `false` when the queue is full or empty.

## Build

```bash
g++ -std=c++17 -O2 -pthread -o spsc_queue spsc_queue.cpp
```

## Per second specs

Measured with one producer thread and one consumer thread pushing and popping 64-byte objects for 1 second (capacity 1024).

| Machine | Objects pushed/s | Objects popped/s |
|---------|------------------|------------------|
| Apple M5 Pro, macOS 26, Apple clang 21, `-O2` | ~8.1M – 8.5M | ~8.1M – 8.5M |
