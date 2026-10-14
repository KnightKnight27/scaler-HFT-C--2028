# Lec-11 Assignment — Lock-based SPSC Queue

A Single-Producer Single-Consumer (SPSC) ring buffer guarded by a **lock**
(spinlock *or* `std::mutex`), with a producer and a consumer thread pushing/popping
**64-byte** objects. The benchmark measures how many objects make it through the
queue in **1 second**.

## Design

- Ring buffer of `Object` (exactly 64 bytes, `static_assert`-checked).
- Capacity is a power of two (`1024`), so `idx % SIZE` becomes the cheaper
  `idx & (SIZE - 1)`.
- The whole queue is protected by one lock. Two lock flavours are compared:
  - `SpinLock` — busy-waits in a `while` loop on a `std::atomic_flag`
    (with a `pause` hint on x86).
  - `std::mutex`.
- `t1` = producer, `t2` = consumer; both are `join()`-ed after the 1s window.

> Note: a *real* SPSC queue would be lock-free (atomics + acquire/release, see
> [`Lec-10/spsc_queue.h`](../../Lec-10/spsc_queue.h)). This assignment is about
> the **locked** version on purpose — to see what locking costs.

## Build & run

```sh
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## Per-second specs

Measured on **Apple M5 Pro (15 cores)**, `g++ -O2 -std=c++17`, 1-second window,
64-byte objects, queue size 1024. "ops/s" = objects successfully pushed **and**
popped per second.

| Lock         | Throughput (objects/sec) | ~M ops/s |
|--------------|--------------------------|----------|
| `SpinLock`   | ~3.5 – 4.6 million       | ~3.5–4.6 |
| `std::mutex` | ~8.5 – 10.3 million      | ~8.5–10.3 |

Numbers vary run to run (OS scheduling, turbo, contention). Three sample runs:

```
run 1:  SpinLock 4.61 M/s   std::mutex 10.32 M/s
run 2:  SpinLock 4.33 M/s   std::mutex 10.03 M/s
run 3:  SpinLock 3.52 M/s   std::mutex  8.51 M/s
```

## Observation

Counter-intuitively, `std::mutex` **beats** the naive spinlock here (~2x).

The producer and consumer both hammer the **same** lock/cache line with no
backoff, so the spinlock turns into a cache-line ping-pong between cores and
burns cycles spinning. `std::mutex` lets a thread that can't make progress
**block and yield** the core instead of busy-spinning, which under this
single-shared-lock contention wins.

Takeaway: a spinlock is only a win when the critical section is tiny *and*
contention is low. Here the lock is contended on every single op, so blocking
beats spinning — and both lose badly to a proper lock-free SPSC design.
