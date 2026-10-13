# Lec-11: SPSC queue with locks

Email: piyush.24bcs10234@sst.scaler.com

Roll no: 24bcs10234

This assignment measures how many 64-byte objects one producer and one consumer can pass through a queue in a second. The same queue is tested with a spinlock and `std::mutex`.

## How it works

The queue uses a ring buffer. Its memory pool is allocated once, so push and pop don't allocate memory. The capacity is rounded up to a power of two so the indices can wrap using a bit mask.

Each push and pop takes the lock. The producer stops after the requested time, and the consumer finishes popping any items left in the queue. Both threads are joined before the result is printed. The rate uses the actual elapsed time, including this final drain.

## Build and run

From this directory:

```bash
clang++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

The defaults are 1 second per run, capacity 1024 and 5 runs for each lock. To change them, pass the duration, capacity and number of runs:

```bash
./spsc_queue 1 16 7
```

## Machine and results

- Intel Core i7-9750H, 6 cores / 12 threads
- 16 GB RAM
- macOS 15.7.7
- Apple clang 17.0.0, with the flags shown above

The recorded results at capacity 1024 on a quiet machine were approximately:

| Lock | Objects per second |
|---|---|
| Spinlock | 18.7 million |
| `std::mutex` | 9.9 million |

The spinlock was faster in these runs. With only 16 slots, both versions were slower. Results also varied with background apps running, so these numbers depend on the machine and its load.

The code pauses briefly after a failed push or pop to give the other thread a chance to make progress.

## Checks

Before benchmarking, the program checks empty and full queues, FIFO order and wraparound. During each run, it checks the sequence number and its checksum, then compares the total pushes and pops. These checks don't verify every order field. A failed check makes the program exit with code 1.

A short ThreadSanitizer run also completed without race reports.
