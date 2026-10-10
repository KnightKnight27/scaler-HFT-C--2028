# Lec-11 Assignment: SPSC Queue with Locks

**Name:** Krritin Keshan  
**Roll No:** 24bcs10122

A single producer single consumer queue that passes 64 byte objects between two threads. It counts how many objects get pushed and popped in 1 second. The same queue runs once with `std::mutex` and once with a `SpinLock` (a while loop on `std::atomic_flag`).

## Files
- `spsc_queue.hpp`: ring buffer queue, templated on the lock type. The buffer is one fixed `std::array` allocated once (the memory pool), so push and pop never call `new` or `delete`. The size is a power of 2, so `idx & (Size-1)` replaces `idx % Size`.
- `main.cpp`: thread `t1` pushes and thread `t2` pops for 1 second. Then `t1.join()` and `t2.join()` run and the counts are printed.

## Build & run
```bash
g++ -std=c++20 -O2 -pthread main.cpp -o spsc_bench
./spsc_bench
```

## Per second specs
Machine: Apple M3 Pro (12 cores), macOS, `g++ -O2` (Apple clang 17), queue size 1024, 5 runs.

64 byte objects popped per second:

| Run | std::mutex | SpinLock |
|-----|-----------:|---------:|
| 1   | 24,639,908 | 25,101,267 |
| 2   | 20,522,327 | 25,671,817 |
| 3   | 23,575,394 | 25,538,853 |
| 4   | 23,899,039 | 26,806,073 |
| 5   | 20,246,214 | 25,173,448 |

- **std::mutex: about 20 to 25 million objects/sec** (about 1.3 to 1.6 GB/s)
- **SpinLock: about 25 to 27 million objects/sec** (about 1.6 to 1.7 GB/s)

The pushed count is never more than 1024 above the popped count, because at most 1024 items can still be in the queue when the timer stops.

## Notes
- Both threads fight over one lock on every push and pop. The lock's cache line moves back and forth between the two cores, and that limits throughput.
- The spinlock never sleeps, so it is a bit faster and its runs vary less. `std::mutex` can park a thread in the kernel when there is contention, which adds latency and makes the runs vary more.
- A lock-free version would drop the lock: the producer owns `mPushIdx` and the consumer owns `mPopIdx`, with acquire/release atomics on each one (see Lec-10/Lec-12).
