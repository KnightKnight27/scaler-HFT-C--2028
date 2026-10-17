# Assignment 1: SPSC Queue with Locks

**Name:** Sanjay Sagar Reddy  
**Roll No:** 24bcs10419  
**Email:** sanjay.24bcs10419@sst.scaler.com  

---

An implementation of a **Single Producer, Single Consumer (SPSC) queue** protected by `std::mutex`, with a real producer thread and a real consumer thread, benchmarked using 64-byte objects.

---

## Design

- **Lock type:** `std::mutex` via `std::lock_guard`
- **Buffer:** `std::vector` circular ring buffer
- **Object size:** 64 bytes (`alignas(64) DataPacket`) -- matches CPU cache line width
- **Concurrency:** 1 dedicated producer thread + 1 dedicated consumer thread
- **Shutdown:** Producer sets `stop_producer` flag; consumer drains the queue fully before exiting

---

## Performance Benchmark Results

Benchmark ran for **~1 second** of sustained load on this machine using **64-byte objects**:

| Metric | Result |
| :--- | ---: |
| **Lock mechanism** | `std::mutex` |
| **Object size** | 64 bytes |
| **Actual elapsed time** | ~1.01 seconds |
| **Objects pushed** | 11,945,386 |
| **Objects popped** | 11,945,386 |
| **Push ops / second** | ~11,869,321 ops/s (**~11.87 Mops/s**) |
| **Pop ops / second** | ~11,869,321 ops/s (**~11.87 Mops/s**) |
| **Consumer throughput** | **~724.45 MiB/s** |

> All pushed packets were successfully consumed -- zero data loss.

---

## How to Build and Run

### Windows (MSYS2 / MinGW g++)
```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue.exe
spsc_queue.exe
```

### Linux / macOS (GCC or Clang)
```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

### Windows (MSVC Developer Command Prompt)
```
cl /std:c++17 /O2 /EHsc spsc_queue.cpp /Fe:spsc_queue.exe
spsc_queue.exe
```
