# Lock-based SPSC Queue: std::mutex vs SpinLock

**Name:** Vansh Malhotra
**Roll No:** 24bcs10268

The assignment (`spsc_queue.cpp`): build a single-producer single-consumer queue protected by a lock (a spinlock or `std::mutex`). One producer thread pushes 64-byte objects and one consumer thread pops them. Join both threads, measure how many objects get through in 1 second, and report the numbers here.

## Deliverables

| Requirement | Where |
|---|---|
| SPSC queue | `LockedRing<T, N, Lock>` in `spsc_queue.hpp` |
| SpinLock (while loop) | `SpinLock` in `spsc_queue.hpp`: test-and-test-and-set on `std::atomic<bool>` |
| `std::mutex` | Same queue, built with `Lock = std::mutex` |
| 64-byte objects | `Packet`, `alignas(64)`, size checked with `static_assert` |
| Memory pool | Fixed `T slots_[N]` array created once with the queue. `push`/`pop` never call `new` or `delete` |
| Producer / consumer + `join()` | `benchmark<Lock>()` in `main.cpp` |
| Objects per second | Printed for each run. Results below |

## How it works
- **Queue:** a ring buffer with a power-of-two capacity (1024). `write_` and `read_` only ever count up. The slot index is `counter & (N - 1)`, the queue is full when `write_ - read_ == N`, and empty when they are equal. Every `try_push` and `try_pop` holds the lock.
- **SpinLock:** `exchange(true)` tries to take the lock. While the lock is held, the waiting thread spins on a plain relaxed load, so it doesn't keep writing to the cache line.
- **Benchmark:** the producer pushes until the main thread sets `time_up` after 1 second. The consumer then drains whatever is left, so **pushed == popped** on every run. Each packet carries a sequence number, and the consumer checks that packets arrive in order with none lost. The `check` column shows `ok` when both conditions hold. The rate is count divided by the measured elapsed time.

## Build & run
```bash
g++ -std=c++17 -O2 -pthread main.cpp -o spsc_bench
./spsc_bench
```

## Results
Apple M4 (10 cores), Apple clang 21, `-O2`, capacity 1024, 5 runs per lock:

| Run | std::mutex (obj/s) | SpinLock (obj/s) |
|-----|-----------:|-----------:|
| 1 | 21,455,442 | 15,271,000 |
| 2 | 21,260,701 |  6,459,978 |
| 3 | 22,402,816 | 10,446,625 |
| 4 | 22,549,265 | 12,152,595 |
| 5 | 22,489,339 |  7,588,543 |

- **std::mutex:** about **21–22.5 million** 64-byte objects per second (~1.4 GB/s). Very steady from run to run.
- **SpinLock:** about **6.5–15 million** objects per second (~0.4–1.0 GB/s). Large swings between runs.
- Every run passed the order and count check.

## Why the mutex wins here
Both threads hit the lock constantly. On macOS, `std::mutex` (`os_unfair_lock` underneath) handles this kind of contention well. With the plain spinlock, one thread can keep grabbing the lock over and over while the other waits, and the cache line holding the lock keeps moving between cores. Throughput then depends on how the scheduler places the two threads, which explains the large run-to-run variation. Expect different numbers on other machines and operating systems.

## Notes
- Exactly one thread may call `try_push` and exactly one may call `try_pop`.
- A lock-free version (atomic head and tail, no lock) would be much faster. The assignment asks for locks, so this version uses them.
