# Lec-11 Assignment: SPSC Queue with Locks

- **name:** Rohan Ranjan
- **email:** rohan.24bcs10428@sst.scaler.com
- **roll_no:** 24BCS10428

## What's in here

`spsc_queue.cpp`: a single-producer / single-consumer queue that passes **64-byte objects** (`Order`, one cache line).

- **Memory pool:** the ring buffer's slots are allocated once (`aligned_alloc`, 64-byte aligned, pre-touched), so the hot path never calls `new` or `delete`.
- **Lock policy:** the queue takes the lock type as a template parameter:
  - `SpinLock`: a `std::atomic_flag` busy-wait (`while` loop with a CPU `yield`/`pause`)
  - `std::mutex`
- **Threads:** one producer thread pushes and one consumer thread pops. They start together, run for 1 second, then `t1.join()` / `t2.join()`.
- **Correctness check:** each object carries a sequence number. The consumer checks that it receives them in order with matching payload (`fifo OK`).
- A lock-free version is included **only as a baseline** for comparison.

## Build & run

```bash
g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc
./spsc        # 5 runs of 1 second per queue type (pass a number to change)
```

## Results (objects pushed **and** popped per second)

Machine: Apple M4 (10 cores), macOS, Apple clang, `-O2`, queue capacity 65,536 (4 MB pool), 5 × 1 s runs.

| Queue | min | **median** | max | bandwidth (median) |
|---|---|---|---|---|
| SpinLock | 24.15 M/s | **25.65 M/s** | 28.43 M/s | 1.64 GB/s |
| std::mutex | 32.81 M/s | **33.16 M/s** | 33.82 M/s | 2.12 GB/s |
| lock-free (baseline) | 142.36 M/s | **143.92 M/s** | 149.79 M/s | 9.21 GB/s |

**With locks, about 25–33 million 64-byte objects per second** go through the queue end to end.

```
SPSC queue, 64-byte objects, capacity 65536, 5 x 1s runs each
numbers = objects pushed AND popped (consumed) per second

spinlock               min    24.15 M/s | median    25.65 M/s | max    28.43 M/s |    1.64 GB/s | fifo OK
std::mutex             min    32.81 M/s | median    33.16 M/s | max    33.82 M/s |    2.12 GB/s | fifo OK
lock-free (baseline)   min   142.36 M/s | median   143.92 M/s | max   149.79 M/s |    9.21 GB/s | fifo OK
```

## Observations

- **Every operation takes the lock**, so the cache line holding the lock bounces between the producer and consumer cores on every push and pop. That bouncing is the main cost: about 30–40 ns per object.
- **The spinlock lost to `std::mutex` here.** On macOS, `std::mutex` wraps a lock that spins briefly before parking the thread. A naive `test_and_set` spinlock hammers the shared cache line with writes while waiting, and that slows down the thread that holds the lock. A test-and-test-and-set loop (spin on a plain load first) would likely narrow the gap. Results on Linux/x86 can differ.
- **The lock-free baseline is about 4–5× faster.** Producer and consumer only touch their own index, and each keeps a cached copy of the other's index, so cross-core traffic happens only occasionally.
- Results depend on the machine, the OS scheduler and which cores the threads land on (no thread pinning on macOS), which is why the min/max spread is shown.
