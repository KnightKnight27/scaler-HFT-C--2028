# SPSC Queue (std::mutex and SpinLock)

**Name:** Prabhdeep Singh  
**Roll No:** 24bcs10235

A single-producer single-consumer queue that passes 64-byte objects between two threads, plus a 1-second throughput benchmark. The same queue is run with two different locks.

## What's in it
- `spsc_queue.hpp`: ring-buffer queue, templated on the lock type. Includes a small `SpinLock` (busy-wait on an `std::atomic_flag`). The buffer is a fixed array allocated once (the memory pool), so there is no `new`/`delete` per push or pop.
- `main.cpp`: for each lock, one producer thread pushes and one consumer thread pops for 1 second. Both threads are joined and the counts are printed.

## Build & run
```bash
g++ -std=c++17 -O2 -pthread main.cpp -o spsc_bench
./spsc_bench
```

## Performance
Machine: Apple M4 (10 cores), `g++ -O2`, queue capacity 1024, 5 runs. Objects pushed per second:

| Run | std::mutex | SpinLock |
|-----|-----------|----------|
| 1   | 8,996,858 | 9,594,789 |
| 2   | 8,101,491 | 8,957,766 |
| 3   | 9,440,543 | 8,406,374 |
| 4   | 9,234,295 | 8,581,865 |
| 5   | 7,490,416 | 11,195,917 |

**About 7.5 to 11 million 64-byte objects per second with either lock** (roughly 0.5 to 0.7 GB/s). Popped counts are within a few hundred of pushed counts (the queue holds at most 1024 items when the timer stops).

The results vary run to run (other runs on this machine have ranged from about 7 to 20 million per second) because the two threads contend for one lock and the OS scheduler moves them between cores. The mutex and spinlock are close to each other and neither wins consistently.

## Notes
SPSC means exactly one thread calls `push` and exactly one calls `pop`. Locks keep it simple and correct but cap throughput because both threads contend on one lock.
