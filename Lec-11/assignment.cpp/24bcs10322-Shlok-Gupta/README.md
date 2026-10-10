# SPSC Queue: Mutex vs SpinLock

**Name:** Shlok Gupta  
**Roll No.:** 24bcs10322

## Overview

This project implements a Single-Producer Single-Consumer (SPSC) queue in C++ and compares the performance of two synchronization mechanisms: `std::mutex` and a custom `SpinLock`.

The queue transfers fixed-size 64-byte objects between a producer thread and a consumer thread. Each benchmark runs for approximately one second and measures the number of successful push and pop operations per second.

## Features

- Fixed-capacity ring buffer with 1,024 object slots.
- 64-byte objects aligned with a typical cache-line size.
- Templated queue supporting different lock implementations.
- Custom spinlock implemented using `std::atomic_flag`.
- Concurrent producer and consumer threads.
- Throughput measurement for both synchronization mechanisms.
- No dynamic memory allocation during individual push and pop operations.

## Project Structure

```text
spsc_queue_assignment/
├── main.cpp
├── spsc_queue.hpp
└── README.md
```

- **main.cpp:** Runs the producer-consumer benchmark and displays performance statistics.
- **spsc_queue.hpp:** Defines the 64-byte object, spinlock, and templated ring-buffer queue.

## Requirements

- C++17-compatible compiler such as GCC.
- Standard C++ threading support.
- Windows with MSYS2 UCRT64, Linux, or macOS.

## Compilation and Execution

### Windows (PowerShell)

Open a terminal in the project directory and execute:

```powershell
g++ -std=c++17 -O2 -pthread .\main.cpp -o .\spsc_bench.exe
.\spsc_bench.exe
```

Verify GCC installation with:

```powershell
g++ --version
```

If `g++` is not recognized, use the MSYS2 UCRT64 terminal or add the compiler's bin directory to your Windows PATH.

### Linux and macOS

```bash
g++ -std=c++17 -O2 -pthread main.cpp -o spsc_bench
./spsc_bench
```

## Implementation Details

### 1. Ring Buffer

The queue uses a fixed-size array to store objects. It maintains three variables:

- `head`: Index of the next object to remove.
- `tail`: Index where the next object will be inserted.
- `count`: Number of objects currently stored.

The `push()` operation returns `false` when the queue is full, and `pop()` returns `false` when the queue is empty. Both operations use a lock to protect shared queue data.

### 2. Synchronization Mechanisms

**std::mutex:** Uses a standard mutual-exclusion lock. The queue acquires it through `std::lock_guard`, which releases it automatically when the operation completes.

**SpinLock:** Uses `std::atomic_flag` and repeatedly checks the flag while waiting for the lock. Acquire and release memory ordering provide synchronization between threads.

### 3. Benchmarking Methodology

1. Create a queue with a capacity of 1,024 objects.
2. Start one producer thread and one consumer thread.
3. Run the test for approximately one second.
4. Signal both threads to stop and join them.
5. Calculate throughput from successful operations and elapsed time.

The benchmark uses `std::chrono::steady_clock` to measure elapsed time.

## Benchmark Results

**Test environment and configuration**

| Parameter | Value |
|---|---|
| Compiler optimization | `-O2` |
| Object size | 64 bytes |
| Queue capacity | 1,024 objects |
| Producer threads | 1 |
| Consumer threads | 1 |
| Benchmark duration | Approximately 1 second per test |

**Actual results obtained on my laptop**

| Metric | std::mutex | SpinLock |
|---|---:|---:|
| Duration | 1.001 s | 1.009 s |
| Successful pushes | 6,072,065 | 3,770,643 |
| Successful pops | 6,071,751 | 3,770,352 |
| Push throughput | 6,063,324 objects/s | 3,738,813 objects/s |
| Pop throughput | 6,063,010 objects/s | 3,738,524 objects/s |

### Performance Analysis

The `std::mutex` implementation achieved approximately 6.06 million pushes per second, while the custom spinlock achieved approximately 3.74 million pushes per second.

Based on these measurements, `std::mutex` delivered approximately **62.2% higher push throughput** than `SpinLock`.

One possible explanation is that busy-waiting consumes CPU resources while threads compete for the shared lock. A mutex implementation may manage contention more efficiently by allowing waiting threads to yield or sleep when appropriate. The exact behavior depends on the operating system and workload.

The push and pop counts differ slightly because the queue may contain objects when the stop signal is issued.

These figures represent a single benchmark run. Repeating the experiment several times and comparing average throughput would provide a more reliable assessment.

## Conclusion

This experiment demonstrates that a custom spinlock is not necessarily faster than `std::mutex`. Spinlocks can be effective when lock holding times are very short and contention is low, but busy-waiting can waste CPU resources when threads frequently compete for the same lock.

In this experiment, `std::mutex` achieved higher throughput than `SpinLock`. The result applies to the tested laptop and workload and should not be treated as a universal performance guarantee.

A possible extension would be to implement a lock-free SPSC queue using atomic producer and consumer indices to reduce shared-lock contention.

## Limitations

- The benchmark uses one producer and one consumer.
- Both threads acquire the same lock for every queue operation.
- Results depend on CPU architecture, compiler, operating system, power settings, and background activity.
- The benchmark measures throughput, not latency or CPU utilization.
- The performance figures represent one run and may vary across repeated tests.
