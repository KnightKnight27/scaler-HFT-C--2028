email: "shambhu.24bcs10356@sst.scaler.com"

roll_no: "24bcs10356"

# Lec-11 Assignment: SPSC Queue with Locks

A single-producer single-consumer (SPSC) queue for 64-byte objects. Thread `t1` pushes and thread `t2` pops for one second. Then both threads are joined and the program counts how many objects went through.

## What's in `spsc_queue.cpp`

- **`Order`**: the 64-byte object (`alignas(64)`, size checked with `static_assert`).
- **`OrderPool<N>`**: the memory pool. It is one pre-allocated array of `N` slots (N = 65,536, a power of two). The hot path never calls `new` or `delete`; slots are reused with `index & (N - 1)`.
- **`LockedSpscQueue<Lock, N>`**: a ring buffer on top of the pool, protected by a lock. It is used with two lock types:
  - `SpinLock`: a `while` loop on `std::atomic<bool>` (test-and-test-and-set, with `yield`/`pause` while waiting).
  - `std::mutex`.
- **`LockFreeSpscQueue<N>`** (bonus): an acquire/release atomic ring buffer with cached indices, for comparison.

Each run also checks that the consumer receives the ids `0, 1, 2, …` in order (FIFO check).

## Machine

- Apple M3 (MacBook), macOS (Darwin 25.5.0), arm64
- Apple clang 17.0.0
- `clang++ -std=c++17 -O3 -Wall -Wextra -pthread`

## Results (64-byte objects, 1 second)

Three back-to-back runs, objects popped per second:

| Queue | Run 1 | Run 2 | Run 3 | Typical |
| :--- | ---: | ---: | ---: | ---: |
| Spinlock (while loop) | 11.88 M/s | 7.52 M/s | 7.91 M/s | **~8–12 M objects/s** (~0.5–0.7 GB/s) |
| `std::mutex` | 30.89 M/s | 30.24 M/s | 29.39 M/s | **~30 M objects/s** (~1.8 GB/s) |
| Lock-free (bonus) | 18.50 M/s | 21.21 M/s | 22.99 M/s | **~18–23 M objects/s** (~1.2–1.4 GB/s) |

All runs passed the FIFO check.

## Observations

- **`std::mutex` beat the spinlock on this machine.** On macOS, a `pthread` mutex spins briefly before it sleeps, and it is not fair: the thread that just released it can take it again right away. As a result, the producer and the consumer each do long runs of pushes or pops while the cache lines stay local. My spinlock lets the two threads alternate almost every operation, so the lock and index cache lines move between cores on nearly every push and pop.
- **The spinlock is the noisiest.** Its result depends a lot on whether both threads run on performance cores or efficiency cores.
- **The lock-free queue is not the fastest here.** It has no lock, but in this benchmark the queue is almost always nearly empty. The consumer keeps catching up with the producer, so every element still causes a cache-line transfer. The mutex version, by contrast, batches its work. With batching or real work between operations, lock-free would win on latency.

## Build and run

```bash
cd Lec-11/assignment.cpp/24bcs10356-Shambhu-Yadav
clang++ -std=c++17 -O3 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```
