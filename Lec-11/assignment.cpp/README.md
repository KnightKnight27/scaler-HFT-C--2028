# SPSC Queue Assignment

Submitted by: Vivaan Goyal  
Roll No: 24BCS10163

This assignment implements a small single-producer, single-consumer queue for fixed-size 64-byte packets. The producer thread pushes items into the buffer while the consumer thread pops from the other side. I kept the code simple and readable instead of over-engineering it.

The queue uses a fixed-size ring buffer with a mutex to guard the shared state. This keeps the logic easy to follow while still matching the assignment requirement of pushing and popping objects between two threads.

## What the code does

- Creates a fixed-capacity ring buffer
- Allows exactly one producer and one consumer
- Stores 64-byte objects in a cache-friendly layout
- Uses `alignas(64)` for packet objects and the queue indices
- Uses `std::thread` to run producer and consumer in parallel
- Reports throughput for a fixed number of queue operations

## Benchmark results

I compiled the current source with MSVC in release mode (`/O2`) and ran it three times on a 13th Gen Intel Core i7-13650HX running Windows 10. Each run transfers 2,000,000 packets. The timer includes thread creation and joining, so these are overall run rates, not timed one-second samples.

| Run | Elapsed | Throughput |
| --- | ---: | ---: |
| 1 | 0.191647 s | 10.44 million packets/s |
| 2 | 0.208913 s | 9.57 million packets/s |
| 3 | 0.193849 s | 10.32 million packets/s |
| Average | 0.198136 s | 10.11 million packets/s |

Each packet is 64 bytes. The program reports completed producer and consumer work indirectly through a fixed operation count; it does not currently print separate push/pop counts or run for exactly one second.

## WSL profiling results

I compiled the current source in Ubuntu 26.04 WSL with GCC 15.2.0 (`-O2`) and ran `perf` 7.0.14. Three benchmark runs reported:

| Run | Elapsed | Throughput |
| --- | ---: | ---: |
| 1 | 0.497296 s | 4.02 million packets/s |
| 2 | 0.483836 s | 4.13 million packets/s |
| 3 | 0.479381 s | 4.17 million packets/s |
| Average | 0.486838 s | 4.11 million packets/s |

The explicit software-counter run (`perf stat -e task-clock,context-switches,cpu-migrations,page-faults -r 3`) reported:

```text
931.83 msec task-clock:u ( +- 1.07% )
         0      context-switches:u
         0      cpu-migrations:u
      4243      page-faults:u ( +- 0.01% )
0.499517333 +- 0.005298440 seconds time elapsed ( +- 1.06% )
```

The default `perf stat -r 3` did not run successfully: WSL does not expose the CPU PMU events requested by this `perf` build (`cpu_atom` event unavailable). As a result, hardware counters such as cycles, instructions, and branch misses could not be collected. These WSL measurements are separate from the Windows/MSVC results above; compiler, operating system, and virtualization differences affect throughput.

Commands used:

```sh
g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o queue_bench
perf stat -e task-clock,context-switches,cpu-migrations,page-faults -r 3 ./queue_bench
```

Results vary with the CPU, operating system, compiler, virtualization, and system load.

## Notes

The queue uses a preallocated vector as its slot pool and a mutex to protect the shared ring-buffer state. The benchmark is intended as a simple local comparison, not a universal performance figure.
