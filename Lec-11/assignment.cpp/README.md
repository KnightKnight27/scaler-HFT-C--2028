# SPSC Queue and Memory Pool Benchmark
email: alisha.24bcs10636@sst.scaler.com
roll_no: 10636
## Overview

This project implements and benchmarks a single-producer, single-consumer (SPSC) queue in C++. The goal is to measure how many 64-byte objects can be transferred per second using different synchronization and memory-management approaches.

The experiments include:

- SPSC queue protected by `std::mutex`
- SPSC queue protected by a custom spinlock
- Producer-consumer queue using a preallocated memory pool
- Producer-consumer queue using dynamic allocation with `new` and `delete`

## Environment

| Property | Details |
|---|---|
| Operating system | Ubuntu 24.04 LTS |
| Compiler | GCC 13.3.0 |
| Language standard | C++20 |
| Optimization | `-O2` |
| Object payload | 64 bytes |
| Total transfers per run | 5,000,000 |
| Queue capacity | 1,024 entries |
| Producer threads | 1 |
| Consumer threads | 1 |

Results depend on CPU hardware, system load, compiler settings, and scheduling.

## Implementations

### 1. Mutex queue

The queue uses `std::mutex` and `std::lock_guard` to protect its shared state. The buffer is preallocated, and the queue supports push and pop operations.

### 2. Spinlock queue

The spinlock uses `std::atomic_flag` with acquire-release memory ordering. A waiting thread repeatedly checks the lock instead of blocking on a mutex.

A spinlock may be useful in some low-latency workloads, but it can waste CPU cycles under contention. It is not guaranteed to outperform a mutex.

### 3. Memory pool

The memory pool preallocates 1,024 objects and maintains a collection of available object pointers. The producer acquires an object, fills it, and pushes its pointer into the queue. The consumer pops the pointer, processes the object, and returns it to the pool.

This approach avoids dynamically allocating a new object for every transfer. Pool acquisition and release are protected by a mutex.

### 4. Dynamic allocation

This implementation allocates an object with `new` for each transfer and releases it with `delete` after consumption. It uses the same bounded pointer-queue approach as the memory-pool benchmark.

The comparison measures these specific implementations, including their synchronization costs. It does not isolate allocation overhead completely.

## Benchmark methodology

Each benchmark transfers 5,000,000 objects and counts successful consumer pops. The reported throughput is calculated as:

`Throughput = completed objects / elapsed seconds`

The object size is verified using `static_assert(sizeof(Object) == 64)`.

The producer and consumer wait for a shared start flag before beginning their queue operations. The timer starts immediately before the flag is released and stops after both threads have joined.

The elapsed time therefore includes the workload and thread completion, but excludes thread creation. Each run is a separate measurement.

## Results

The following results were measured on the local Ubuntu environment. Throughput is rounded to three decimal places in millions of completed objects per second.

### Mutex queue

| Run | Elapsed time (s) | Throughput (million objects/s) |
|---|---:|---:|
| 1 | 1.29153 | 3.871 |
| 2 | 1.28297 | 3.897 |
| 3 | 1.29144 | 3.872 |
| 4 | 1.31333 | 3.807 |
| 5 | 2.17545 | 2.298 |
| **Mean** | **1.47094** | **3.549** |

### Spinlock queue

| Run | Elapsed time (s) | Throughput (million objects/s) |
|---|---:|---:|
| 1 | 0.98231 | 5.090 |
| 2 | 1.52188 | 3.285 |
| 3 | 1.18803 | 4.209 |
| 4 | 1.12270 | 4.454 |
| 5 | 1.25700 | 3.978 |
| 6 | 2.27459 | 2.198 |
| 7 | 1.41944 | 3.523 |
| 8 | 1.27123 | 3.933 |
| 9 | 1.97768 | 2.528 |
| 10 | 1.66288 | 3.007 |
| **Mean throughput** | — | **3.620** |

### Memory pool

| Run | Elapsed time (s) | Throughput (million objects/s) |
|---|---:|---:|
| 1 | 2.81649 | 1.775 |
| 2 | 2.48841 | 2.009 |
| 3 | 3.14446 | 1.590 |
| 4 | 2.96415 | 1.687 |
| **Mean** | **2.85338** | **1.765** |

### Dynamic allocation

| Run | Elapsed time (s) | Throughput (million objects/s) |
|---|---:|---:|
| 1 | 2.54493 | 1.965 |
| 2 | 2.51262 | 1.990 |
| 3 | 2.34806 | 2.129 |
| 4 | 3.12319 | 1.601 |
| 5 | 2.57873 | 1.939 |
| **Mean** | **2.62151** | **1.925** |

## Observations

- All benchmark runs completed 5,000,000 consumer pops.
- The measured mutex and spinlock throughput varied across runs.
- The spinlock's mean throughput was slightly higher than the mutex's mean in these measurements, but the results do not establish that spinlocks are generally faster.
- The dynamic-allocation implementation had a higher mean throughput than the memory-pool implementation in these tests.
- The memory-pool benchmark has additional synchronization for acquiring and returning objects. This overhead may offset the benefit of avoiding repeated allocations.
- The results should be treated as measurements of these particular implementations, not universal performance claims.

## Build and run

Run the following commands from the repository root.

### Mutex queue

```bash
g++ -std=c++20 -O2 -Wall -Wextra -pthread \
    Lec-11/assignment.cpp/spsc_queue.cpp \
    -o /tmp/spsc_mutex

/tmp/spsc_mutex
```

### Spinlock queue

```bash
g++ -std=c++20 -O2 -Wall -Wextra -pthread \
    Lec-11/assignment.cpp/spinlock_queue.cpp \
    -o /tmp/spsc_spin

/tmp/spsc_spin
```

### Memory pool demonstration

```bash
g++ -std=c++20 -O2 -Wall -Wextra -pthread \
    Lec-11/assignment.cpp/memory_pool.cpp \
    -o /tmp/memory_pool

/tmp/memory_pool
```

### Memory pool benchmark

```bash
g++ -std=c++20 -O2 -Wall -Wextra -pthread \
    Lec-11/assignment.cpp/memory_pool_benchmark.cpp \
    -o /tmp/memory_pool_benchmark

/tmp/memory_pool_benchmark
```

### Dynamic-allocation benchmark

```bash
g++ -std=c++20 -O2 -Wall -Wextra -pthread \
    Lec-11/assignment.cpp/dynamic_allocation_benchmark.cpp \
    -o /tmp/dynamic_allocation_benchmark

/tmp/dynamic_allocation_benchmark
```

## Conclusion

This assignment explores the implementation and benchmarking of a bounded SPSC queue, mutexes, spinlocks, and reusable memory. The experiments demonstrate that synchronization overhead, memory-management strategy, and system scheduling can affect measured throughput.

The reported values are local benchmark results. Further work could include CPU affinity, more repeated trials, latency percentiles, reduced false sharing, and a lock-free SPSC implementation for comparison.

