# Lec-11 — SPSC Queue (64-byte objects)

A bounded **Single-Producer / Single-Consumer** ring-buffer queue, guarded by a
lock, that moves fixed **64-byte objects** between a producer thread and a
consumer thread.

## Design

- **Ring buffer = memory pool.** The queue owns a fixed array of 64-byte slots.
  Objects are copied in/out of those slots, so there is no `new`/`delete` on the
  hot path — the buffer itself is the pool.
- **Two lock variants** for comparison:
  - `SpinlockSpscQueue` — busy-waits on a `std::atomic_flag` (while-loop spinlock).
  - `MutexSpscQueue` — uses `std::mutex` + `std::lock_guard`.
- **Threads:** one producer (`t1`) pushes, one consumer (`t2`) pops; both are
  joined with `t1.join()` / `t2.join()`.
- `head_` / `tail_` are cache-line aligned (`alignas(64)`) to avoid false
  sharing between the two threads.

## Build & Run

```bash
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

Each variant runs the producer + consumer concurrently for **1 second** and
reports how many 64-byte objects were pushed and popped.

## Per-second specs

Ring capacity: 1024 slots (64 KB pool). Measured over a 1-second run:

| Lock variant             | Objects / second | Throughput   |
| ------------------------ | ---------------- | ------------ |
| Spinlock (`atomic_flag`) | ~22.3 million    | ~1.43 GB/sec |
| `std::mutex`             | ~19.5 million    | ~1.25 GB/sec |

> Numbers vary with CPU, load, and scheduler. On this run the spinlock was
> ~14% faster than the mutex for this tiny, uncontended critical section.

Measured on: Apple Silicon (macOS), `g++ -O2`.
