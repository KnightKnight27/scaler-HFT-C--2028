# Lock-Based SPSC Queue Benchmark

## Implementation Overview

- **Data Structure**: Fixed-capacity ring buffer array (`SPSCQueue<1024>`)
- **Payload**: 64-byte aligned struct (`Object64`)
- **Synchronization**: `std::mutex` with `std::lock_guard`
- **Thread Lifecycle**: `t1` (Producer) and `t2` (Consumer) with 1-second execution timer and explicit `.join()`

## Hardware & System Specs

- **CPU**: Apple M2
- **Compiler**: g++-16 (Homebrew GCC 16.2.0) 16.2.0 (`-O3` optimized)
- **OS**: macOS 27 Golden Gate 27.0.1

## Performance Benchmark (1 Second Execution)

- **Successful Pushes**: 21,535,531 ops/sec
- **Successful Pops** : 21,535,162 ops/sec
- **Total Throughput**: 43,070,693 ops/sec
