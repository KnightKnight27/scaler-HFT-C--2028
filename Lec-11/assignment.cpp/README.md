# Lec-11 SPSC Queue Benchmark

## Author

- **Name:** Sourabh Srivastva
- **Roll number:** 24BCS10157
- **Email:** sourabh.24bcs10157@sst.scaler.com

## Overview

This assignment implements a bounded single-producer, single-consumer queue. It compares two locking options, `SpinLock` and `std::mutex`, while transferring 64-byte objects between a producer thread and a consumer thread.

The queue uses a preallocated memory pool, and the benchmark measures push and pop throughput over a one-second interval. Both worker threads are joined after each run.

## Design

### Object

Each `Object` is 64 bytes and aligned to 64 bytes. It contains a sequence number, a timestamp, and a 48-byte payload. The code uses `static_assert` to check the object size and alignment.

The alignment is intended to keep each object in its own 64-byte slot. It does not make 64-byte reads or writes atomic.

### Locks

The queue can use either lock policy:

- **`SpinLock`** uses `std::atomic_flag` in a loop and a CPU pause hint where available.
- **`std::mutex`** uses `std::lock_guard` to release the lock automatically.

Both the producer and consumer use the same lock to access the queue.

### Memory pool and queue

The memory pool allocates its slots when the queue is constructed. For the benchmark’s `Object` type, queue operations reuse those slots and do not allocate memory on the hot path.

The queue is a circular buffer with a power-of-two capacity. It uses masked indices to select slots. `push` returns `false` when the queue is full; `pop` returns `false` when it is empty.

## Benchmark method

Each configuration runs three trials, with a one-second measurement window per trial. The timer uses `std::chrono::steady_clock`. A successful push or pop is counted in the timed results only when the operation completes by the deadline. Pops completed after the deadline are reported separately as drain operations.

The benchmark checks FIFO sequence order through both the timed period and the drain. The clock checks used to classify operation completion add some measurement overhead, so results may differ from runs using a less precise boundary.

## Build and run

Compile with `g++`:

```bash
g++ -std=c++17 -O3 -pthread -Wall -Wextra spsc_queue.cpp -o spsc_queue.exe
```

Run the queue checks:

```powershell
.\spsc_queue.exe --test
```

Run the benchmark:

```powershell
.\spsc_queue.exe
```

The benchmark runs 12 trials total: three trials for each combination of lock type and queue capacity.

## Benchmark results

**Environment:** Windows 11, AMD Ryzen 9, GCC via MinGW-w64, compiled with `-std=c++17 -O3 -pthread`.

### Capacity: 1,024 slots

| Trial | Lock | Timed pushes | Timed pops | Drain pops | Pushes/s | Pops/s | MB/s | FIFO |
|---:|---|---:|---:|---:|---:|---:|---:|---|
| 1 | SpinLock | 9,007,927 | 9,006,904 | 1,023 | 9.01 M | 9.01 M | 549.7 | OK |
| 2 | SpinLock | 9,014,295 | 9,013,272 | 1,023 | 9.01 M | 9.01 M | 550.1 | OK |
| 3 | SpinLock | 9,199,984 | 9,198,961 | 1,023 | 9.20 M | 9.20 M | 561.5 | OK |
| **Average** | **SpinLock** | — | — | — | **9.07 M** | **9.07 M** | **553.8** | **All passed** |
| 1 | `std::mutex` | 3,923,178 | 3,922,154 | 1,024 | 3.92 M | 3.92 M | 239.4 | OK |
| 2 | `std::mutex` | 3,508,193 | 3,507,170 | 1,023 | 3.51 M | 3.51 M | 214.1 | OK |
| 3 | `std::mutex` | 3,587,451 | 3,586,443 | 1,009 | 3.59 M | 3.59 M | 218.9 | OK |
| **Average** | **`std::mutex`** | — | — | — | **3.67 M** | **3.67 M** | **224.1** | **All passed** |

### Capacity: 65,536 slots

| Trial | Lock | Timed pushes | Timed pops | Drain pops | Pushes/s | Pops/s | MB/s | FIFO |
|---:|---|---:|---:|---:|---:|---:|---:|---|
| 1 | SpinLock | 9,279,568 | 9,214,032 | 65,536 | 9.28 M | 9.21 M | 562.4 | OK |
| 2 | SpinLock | 9,098,399 | 9,032,863 | 65,536 | 9.10 M | 9.03 M | 551.3 | OK |
| 3 | SpinLock | 9,194,685 | 9,129,149 | 65,536 | 9.19 M | 9.13 M | 557.2 | OK |
| **Average** | **SpinLock** | — | — | — | **9.19 M** | **9.13 M** | **557.0** | **All passed** |
| 1 | `std::mutex` | 4,198,601 | 4,140,480 | 58,121 | 4.20 M | 4.14 M | 252.7 | OK |
| 2 | `std::mutex` | 4,093,502 | 4,028,547 | 64,956 | 4.09 M | 4.03 M | 245.9 | OK |
| 3 | `std::mutex` | 4,199,331 | 4,133,801 | 65,531 | 4.20 M | 4.13 M | 252.3 | OK |
| **Average** | **`std::mutex`** | — | — | — | **4.16 M** | **4.10 M** | **250.3** | **All passed** |

### Summary

| Capacity | Lock | Average pushes/s | Average pops/s | Average throughput |
|---:|---|---:|---:|---:|
| 1,024 | SpinLock | 9.07 M | 9.07 M | 553.8 MB/s |
| 1,024 | `std::mutex` | 3.67 M | 3.67 M | 224.1 MB/s |
| 65,536 | SpinLock | 9.19 M | 9.13 M | 557.0 MB/s |
| 65,536 | `std::mutex` | 4.16 M | 4.10 M | 250.3 MB/s |

In this run, SpinLock had the highest average throughput at both capacities. These results describe this machine and run; they should not be treated as a general ranking of the two lock types.

## Queue checks

Running `.\spsc_queue.exe --test` checks:

- Popping from an empty queue
- Basic push and pop behavior
- Full-queue handling
- FIFO order
- Ring-buffer wraparound
- Rejection of zero and non-power-of-two capacities

## Limitations

- This is a lock-based queue, not a lock-free queue.
- Results depend on the CPU, operating system, compiler, and system load.
- The memory pool is preallocated ring-buffer storage; it is not a separate free-list allocator.
- The benchmark’s deadline checks add overhead to each operation.