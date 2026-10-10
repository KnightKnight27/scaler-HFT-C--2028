# SPSC Queue Assignment

**Name:** Juhil Modi  
**Roll no:** 10310  
**Email:** juhil.24bcs10310@sst.scaler.com  

## What this assignment is about

For this assignment I made a small **single producer, single consumer queue**. I wanted to see how many 64-byte objects can move through the queue in one second when the queue is protected by two different locks.

I kept the queue in two files so the comparison is easy:

- `spsc_spinlock.cpp` uses a spinlock made with `std::atomic<bool>`.
- `spsc_mutex.cpp` uses the normal `std::mutex`.

The queue logic is the same in both files. Only the type of lock is different.

## How my queue works

The object being pushed is an `Order`. It contains one `long long` id and a 56-byte character array, so the total size is exactly 64 bytes. The `static_assert` in the code checks this when the program is compiled.

The queue is a ring buffer with 1024 slots. Since 1024 is a power of two, the code uses a bitwise `&` to find the slot instead of the modulo operator. The complete buffer is allocated once when the queue is created, so the producer and consumer do not allocate memory for every object.

The queue keeps two increasing indexes:

- `mPushIdx` points to the next position for the producer.
- `mPopIdx` points to the next position for the consumer.

The indexes are not reset after the queue wraps around. The current number of objects is the difference between the two indexes. A push fails when the queue is full, and a pop fails when it is empty.

Both operations take the lock, do their work, update the correct index, and then release the lock. The queue cannot be copied or moved because it is meant to be shared by the producer and consumer threads.

## The two lock versions

### Spinlock

The spinlock keeps checking the atomic flag in a loop until it can change the flag from `false` to `true`. It does not put the thread to sleep while the lock is busy. The acquire and release memory orders make sure the queue work stays inside the lock.

This can be useful when the lock is held for a very short time, but the waiting thread keeps using CPU while it spins.

### `std::mutex`

The mutex version uses `std::mutex` in the same places as the spinlock. If another thread already owns the mutex, the waiting thread can sleep until the mutex becomes available. This saves CPU while waiting, but entering and leaving the operating system can add overhead.

## Test setup

The producer thread creates orders with ids `0`, `1`, `2`, and so on. The consumer checks that the ids arrive in exactly that order. The main thread lets both threads run for one second, sets the stop flag, and joins both threads before printing the result.

The push and pop counters are aligned to 64 bytes so that the two threads do not unnecessarily share the same cache line. The output also reports whether the order check passed.

I compiled and ran the programs in WSL with these commands:

```bash
g++ -O0 -std=c++17 -pthread spsc_spinlock.cpp -o spin
g++ -O0 -std=c++17 -pthread spsc_mutex.cpp -o mtx

./spin
./mtx
```

I used `-O0` because the assignment was done with no compiler optimisation, as in the class examples.

## My system

- **CPU:** Intel Core i7-1255U (12th gen, 10 cores, 12 threads)
- **OS:** Ubuntu 26.04 running through WSL2 on Windows 11
- **Compiler:** g++ 15.2.0
- **Compiler flags:** `-O0 -std=c++17 -pthread`

## Throughput results

I ran the spinlock and mutex programs six times, one after another. The table shows the number of objects popped during each one-second run.

| Run | Spinlock | `std::mutex` |
| ---: | ---: | ---: |
| 1 | 5,455,694 | 3,798,211 |
| 2 | 4,438,644 | 4,048,786 |
| 3 | 4,595,389 | 3,873,124 |
| 4 | 5,861,718 | 2,799,809 |
| 5 | 3,002,028 | 4,390,216 |
| 6 | 4,506,413 | 3,297,459 |
| **Average** | **about 4.6 million** | **about 3.7 million** |

The number pushed was almost the same as the number popped. The small difference comes from objects that were still in the queue when the stop flag was set. The order check passed in every run.

For this test, the spinlock moved roughly **4.6 million 64-byte objects per second**, which is about **300 MB/s**. The mutex moved roughly **3.7 million objects per second**, or about **240 MB/s**.

## `perf stat` observations

I also used `perf stat` for a few runs:

```bash
perf stat -e task-clock,context-switches,page-faults,cycles,instructions,branch-misses ./spin
perf stat -e task-clock,context-switches,page-faults,cycles,instructions,branch-misses ./mtx
```

The hardware counters for cycles, instructions, and branch misses were reported as unsupported in WSL2. The useful observations were:

- The spinlock used close to 2000 ms of CPU time because both threads kept running while waiting.
- The mutex used less CPU time, but it had around 0.7 seconds of system time in these runs because a waiting thread could enter the kernel.
- Page faults stayed close to 163 for both versions. The queue is allocated once, so push and pop do not keep creating memory pages.
- The reported context-switch count was zero because the available `perf` setup was counting user-mode events. That number should not be read as proof that no kernel scheduling happened.

## What I understood from the comparison

The spinlock was faster on average for this particular test because each critical section is very small. Spinning for a short time cost less than putting a thread to sleep and waking it again. At the same time, the spinlock keeps a CPU busy even when it is waiting.

The results were not identical every time. They changed depending on which cores the threads used and what else was running on the laptop. In one run the mutex was faster, so the average is more useful than any single run.

The next experiment I would try is the lock-free SPSC version from class, with separate atomic indexes. That would let the producer and consumer work without taking the same lock, but that is outside the two implementations submitted here.

## Files in this submission

- `spsc_spinlock.cpp` — SPSC queue protected by the custom spinlock.
- `spsc_mutex.cpp` — SPSC queue protected by `std::mutex`.
- `readme.md` — explanation of the design, test method, results, and observations.


