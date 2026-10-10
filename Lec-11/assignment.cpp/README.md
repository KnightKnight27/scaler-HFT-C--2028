# SPSC Queue: Mutex vs Spinlock vs Lock-Free

**Name:** Namami Verma
**Email** namami.24bcs10349@sst.scaler.com
**Roll Number:** 24bcs10349

## What this is

A Single Producer Single Consumer (SPSC) queue in C++. One thread pushes 64-byte objects, another thread pops them. I run it for 1 second and count how many objects go through.

I made three versions:

- `spsc_queue.cpp`: protected with `std::mutex`
- `spsc_queue_spinlock.cpp`: protected with my own spinlock (`std::atomic_flag` + a `while` loop)
- `spsc_queue_lockfree.cpp`: no lock at all, just atomic indices with acquire/release ordering

## Setup

| Setting | Value |
|---|---|
| Object | `Message`, 64 bytes (checked with `static_assert`) |
| Queue capacity | 1,024 objects |
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

| | Mutex | Spinlock | Lock-free |
|---|---:|---:|---:|
| Successful pushes | 1,165,095 | 1,697,937 | 2,204,756 |
| Successful pops | 1,165,092 | 1,697,937 | 2,203,758 |
| Push throughput (objects/s) | 1,164,710 | 1,697,400 | 2,203,970 |
| Pop throughput (objects/s) | 1,164,710 | 1,697,400 | 2,202,980 |

The lock-free version also printed `Atomic indices are lock-free: true`.

## Observations

- **Spinlock vs mutex:** spinlock was about **1.46x** faster (~45.7% more).
- **Lock-free vs mutex:** about **1.89x** faster (~89.2% more).
- **Lock-free vs spinlock:** about **1.30x** faster (~29.8% more).

So the order was lock-free > spinlock > mutex.

**Why I think this happened:**

- **Mutex is slowest.** The two threads fight for the lock all the time. When one has to wait, the OS can put it to sleep and wake it up later, and that costs time.
- **Spinlock is faster than mutex.** Push and pop are very short, so the lock is free again almost immediately. Spinning is cheaper than sleeping and waking up. The catch is that the waiting thread keeps using the CPU.
- **Lock-free is fastest.** The producer and consumer don't block each other, so they can work at the same time. The producer only writes the push index, the consumer only writes the pop index. A few other things help too:
  - `alignas(64)` puts the two indices on different cache lines (no false sharing)
  - The size is a power of 2, so `index & (size - 1)` replaces `%`
  - `pop()` returns a pointer instead of copying the object

**Push and pop counts are a bit different.** The threads stop at slightly different moments, so a few objects are left in the queue. The difference is always smaller than the capacity (1,024).

## How the lock-free version works

- Producer owns the push index, consumer owns the pop index.
- A thread reads its own index with `relaxed`, since nobody else changes it.
- Producer writes the object, then does a `release` store on the push index. Consumer does an `acquire` load on it, so it is guaranteed to see the object.
- Consumer calls `release()` when it's done with an object. That's a `release` store on the pop index, and the producer's `acquire` load tells it the slot is free to reuse.
- `pop()` gives a pointer to the slot. The consumer must finish using it **before** calling `release()`.
- Indices only ever go up. The queue size is `pushIdx - popIdx`, which still works when the numbers wrap around.

## Limitations

- **These numbers are rough.** The mutex and spinlock results were taken without `-O2`, and the lock-free result was from a separate run. The ratios are only a rough guide.
- **One run each.** I didn't repeat the runs, and thread scheduling changes the numbers every time.
- **Timer overhead.** Each loop calls `steady_clock::now()`, which is slow, so the absolute numbers are lower than they could be. The timer also starts before the threads are created.
- **Spinlock wastes CPU.** When the queue is full or empty, the thread keeps spinning on the lock. It also needs at least 2 free cores.
- **Locked versions block each other.** Producer and consumer can't touch the queue at the same time.
- **No separate memory pool.** I use a `std::vector` that is allocated once, so slots are reused without new allocations.
- **Only 1 producer and 1 consumer.** The lock-free queue breaks with more threads.
- **Payload isn't checked.** The messages are all zeros, so I don't verify order or contents.

## Build and run

```bash
g++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc_queue
g++ -std=c++20 -O2 -pthread spsc_queue_spinlock.cpp -o spsc_queue_spinlock
g++ -std=c++20 -O2 -pthread spsc_queue_lockfree.cpp -o spsc_queue_lockfree

./spsc_queue
./spsc_queue_spinlock
./spsc_queue_lockfree
```

`-std=c++20` is needed because the lock-free version uses `[[likely]]` / `[[unlikely]]`.

## Summary

- Lock-free had the highest throughput, then spinlock, then mutex.
- Spinlock beat mutex because the critical section is tiny.
- Lock-free beat both because the two threads don't wait on each other.