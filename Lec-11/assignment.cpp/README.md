# SPSC Queue: Mutex vs Spinlock

**Name:** Namami Verma
**Email:** namami.24bcs10349@sst.scaler.com
**Roll Number:** 24bcs10349

## What this is

A Single Producer Single Consumer (SPSC) queue in C++. One thread pushes 64-byte objects, another thread pops them. I run it for 1 second and count how many objects go through.

I made two versions:

- `spsc_queue.cpp`: protected with `std::mutex`
- `spsc_queue_spinlock.cpp`: protected with my own spinlock (`std::atomic_flag` + a `while` loop)

## What the assignment asked for

| Requirement | Where it's done |
|---|---|
| SPSC queue | `SPSC<T>` class, ring buffer with push and pop |
| Spinlock (while loop) or `std::mutex` | Both done, one file each |
| Producer and consumer threads, `t1.join()` / `t2.join()` | `main()` in both files |
| Push and pop 64-byte objects | `Message` struct, checked with `static_assert` |
| Count how many in 1 second | Loop runs until a 1 second deadline, then prints objects/second |
| README with per second numbers | This file |
| Git PR | Raised for this repo |

## Setup

| Setting | Value |
|---|---|
| Object | `Message`, 64 bytes (checked with `static_assert`) |
| Queue capacity | 1,024 objects (must be a power of 2) |
| Producer / consumer threads | 1 / 1 |
| Duration | 1 second |
| Storage | one `std::vector` allocated once, no per-push allocation |

| Machine | |
|---|---|
| CPU | `<fill in>` |
| Cores | `<fill in>` |
| OS | `<fill in>` |
| Compiler | `<g++ version>` |

## Results

| | Mutex | Spinlock |
|---|---:|---:|
| Successful pushes | 1,165,095 | 1,697,937 |
| Successful pops | 1,165,092 | 1,697,937 |
| Push throughput (objects/s) | 1,164,710 | 1,697,400 |
| Pop throughput (objects/s) | 1,164,710 | 1,697,400 |

## Observations

- The spinlock version was about **1.46x** faster than the mutex version (~45.7% more objects per second).

**Why I think this happened:**

- **Mutex is slower.** The two threads fight for the lock all the time. When one has to wait, the OS can put it to sleep and wake it up later, and that costs time.
- **Spinlock is faster.** Push and pop are very short, so the lock is free again almost immediately. Spinning is cheaper than sleeping and waking up.
- **But spinlock isn't always better.** The waiting thread keeps using the CPU the whole time. If the lock was held for longer, or there were fewer free cores than threads, the spinlock could do worse than the mutex.

**Push and pop counts are a bit different.** The threads stop at slightly different moments, so a few objects are left in the queue. The difference is always smaller than the capacity (1,024).

## Limitations

- **These numbers are rough.** Both results were taken without `-O2`. They used the same flags so the comparison is fair, but the absolute numbers would be higher with `-O2`.
- **One run each.** I didn't repeat the runs, and thread scheduling changes the numbers every time.
- **Timer overhead.** Each loop calls `steady_clock::now()`, which is slow, so the absolute numbers are lower than they could be. The timer also starts before the threads are created.
- **Spinlock wastes CPU.** When the queue is full or empty, the thread keeps spinning on the lock. It also needs at least 2 free cores.
- **Both versions block each other.** Producer and consumer can't touch the queue at the same time, because they share one lock. `pop()` also copies the object out.
- **Only 1 producer and 1 consumer.** The queue is not made for more threads.
- **Payload isn't checked.** The messages are all zeros, so I don't verify order or contents.

## Build and run

```bash
g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
g++ -std=c++17 -O2 -pthread spsc_queue_spinlock.cpp -o spsc_queue_spinlock

./spsc_queue
./spsc_queue_spinlock
```

## Summary

- Spinlock had higher throughput than mutex in my run (about 45.7% more).
- It's likely because push and pop are very short, so spinning beats sleeping.
- I need more runs with the same flags before saying this is true in general.