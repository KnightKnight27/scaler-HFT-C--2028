# SPSC Queue Benchmark

email: "divijaa.24bcs10206@sst.scaler.com"
roll_no: "24bcs10206"

## Overview

This project implements a Single Producer, Single Consumer (SPSC) queue in C++.

It uses a spinlock to synchronize access to the queue and measures how many 64-byte objects can be pushed and popped per second.

## Features

- Single producer and single consumer threads.
- Fixed-size ring buffer with a capacity of 1024 objects.
- Spinlock implemented using `std::atomic_flag`.
- 64-byte objects using `std::array<char, 64>`.
- Preallocated memory to avoid allocation during push and pop operations.
- Push and pop throughput measured over approximately one second.

## Implementation

The producer continuously pushes objects into the queue, while the consumer removes them.

The spinlock ensures that only one thread accesses the queue at a time.

The queue uses two indices:

- `mPushIdx` tracks the next position for pushing an object.
- `mPopIdx` tracks the next position for popping an object.

The queue reuses its allocated storage instead of allocating memory for every operation.


## Benchmark Results

**Measured results**

| Metric | Result |
|---|---:|
| Duration | 1.01343 seconds |
| Objects pushed | 5693884 |
| Objects popped | 5693397 |
| Pushes per second | 5.61843e+06 |
| Pops per second | 5.61795e+06 |

![alt text](image.png)

## Conclusion

This benchmark measures the throughput of a fixed-size SPSC queue protected by a spinlock. It provides a starting point for studying synchronization and memory reuse in high-throughput C++ programs.
