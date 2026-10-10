# SPSC Queue and Lock Benchmark

- **Name:** Utkarsh Raj
- **Roll no.:** 24BCS10318
- **Email:** `utkarsh.24bcs10318@sst.scaler.com`

This project compares a bounded ring queue protected by a custom `SpinLock` and by `std::mutex`.

## Queue

- `SPSCQ<1024, Lock>` is a fixed-capacity, 1024-entry circular queue.
- `push()` returns `false` when the queue is full; `pop()` returns `false` when it is empty.
- `Object64` contains eight `uint64_t` values. The `static_assert` in the header checks that `sizeof(Object64)` is exactly 64 bytes on the build target.
- `main.cpp` runs a one-producer, one-consumer count benchmark once with each lock.

## Build and Run

With MinGW-w64 GCC on Windows:

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread main.cpp -o spsc_queue.exe
./spsc_queue.exe
```

The measurements below were built with these flags and GCC 15.2.0.

## System and Hardware

- OS: Windows 11, version 25H2, OS build 26200
- Device: Acer Nitro ANV15-51
- CPU: 13th Gen Intel Core i5-13420H
- Architecture: x64
- Logical processors visible to the benchmark process: 12
- Installed memory: 16 GB (15.71 GiB detected)
- Compiler: MinGW-w64 GCC 15.2.0

## Benchmark Results

Each lock was measured for one second per run. The figures are successful queue operations counted by the program; treat them as approximate operations per second. Five process runs were performed. The program always tested `SpinLock` first and `std::mutex` second.

| Lock | Median pushes/s | Median pops/s | Median combined operations/s |
| --- | ---: | ---: | ---: |
| `SpinLock` | 2,641,878 | 2,641,109 | 5,282,987 |
| `std::mutex` | 10,552,650 | 10,552,649 | 21,105,299 |

In these runs, `std::mutex` achieved about **4.0x** the median combined throughput of the custom spin lock.

### Individual Runs

| Run | SpinLock pushes | SpinLock pops | `std::mutex` pushes | `std::mutex` pops |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 3,236,024 | 3,236,022 | 10,552,650 | 10,552,649 |
| 2 | 3,285,762 | 3,285,761 | 10,890,762 | 10,890,762 |
| 3 | 2,291,889 | 2,290,872 | 10,172,516 | 10,172,515 |
| 4 | 2,641,878 | 2,641,109 | 9,377,247 | 9,377,246 |
| 5 | 1,853,889 | 1,853,800 | 10,684,268 | 10,684,267 |

These are results from this machine, compiler, build configuration, and test. They are not general performance guarantees. The benchmark is a short tight loop, always tests locks in the same order, and counts operations without validating payload contents or FIFO order. It can also stop with entries still in the queue, so push and pop counts may differ slightly.
