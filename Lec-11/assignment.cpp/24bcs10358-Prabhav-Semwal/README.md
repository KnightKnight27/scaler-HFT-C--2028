# Lec-11 SPSC Queue

Name: Prabhav Semwal

```text
email: "prabhav.24bcs10358@sst.scaler.com"
roll_no: "24bcs10358"
```

## Files

```text
Lec-11/assignment.cpp/24bcs10358-Prabhav-Semwal/
  main.cpp
  spsc_queue.h
  spsc_queue.cpp
  README.md
```

`spsc_queue.h` declares the message and queue. `spsc_queue.cpp` implements push and pop. `main.cpp` runs one producer and one consumer for approximately one second and joins both threads.

## Build and run

From the repository root:

```bash
clang++ -std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic \
  Lec-11/assignment.cpp/24bcs10358-Prabhav-Semwal/main.cpp \
  Lec-11/assignment.cpp/24bcs10358-Prabhav-Semwal/spsc_queue.cpp \
  -o /tmp/prabhav-spsc-bench

/tmp/prabhav-spsc-bench
```

## Implementation

The queue uses a fixed buffer of 1,024 messages as a preallocated memory pool. Each message has eight 64-bit words, making it 64 bytes. Slots are reused without allocating memory during push or pop.

A shared `std::mutex` protects the buffer and its head, tail and count. Push returns false when full; pop returns false when empty. Both indices wrap using modulo. The first word stores a sequence number, which the consumer checks for FIFO order.

Main starts the producer and consumer, sleeps for one second, sets an atomic stop flag and joins both threads. Successful pushes and pops are counted separately. Throughput is successful pops divided by actual elapsed seconds, including thread startup, the sleep and joining. Messages left in the queue are not drained or counted as completed transfers.

## Results

Local run on 2026-10-10: macOS 27.0, ARM64, Apple Clang 21.0.0, `-O2`, 1,024 slots, one producer and one consumer. Threads were not pinned and background load was uncontrolled.

| Elapsed seconds | Pushed | Popped | Objects/sec | FIFO |
| ---: | ---: | ---: | ---: | --- |
| 1.00511 | 27,151,047 | 27,150,050 | approximately 27.01 million | PASS |

The displayed elapsed time is rounded; throughput uses the unrounded duration. These are results from one run and will vary between runs. The difference between pushes and pops is the number of messages left in the queue.
