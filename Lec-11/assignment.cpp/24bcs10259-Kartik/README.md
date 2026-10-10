# SPSC Queue - Lec 11 Assignment

name: Kartik

email: "kartik.24bcs10259@sst.scaler.com"

roll_no: "10259"

## What I did

Single producer single consumer queue (ring buffer of 1024 slots) that pushes and pops
64 byte objects (`struct Order`, `sizeof == 64`). Producer and consumer are two
`std::thread`s, run for 1 second, then `stop` is set and both are `join()`ed.
The consumer checks the ids come out in the same order they went in.

Three versions in `spsc_queue.cpp`:

1. **spinlock** - `LockQueue<SpinLock>`, lock is a while loop on `std::atomic<bool>::exchange`
   (later changed to spin on a plain load first, that took it from ~3.2M to ~4.3M ops/sec)
2. **std::mutex** - same `LockQueue` but with `std::mutex`
3. **atomics** - no lock, `head` and `tail` are atomics (only consumer writes head, only
   producer writes tail), `alignas(64)` so they are on different cache lines, and each side
   keeps a cached copy of the other index. First version without the cached copy was only
   ~7-10M ops/sec, after caching it went to 30-60M.

## How to run

```
clang++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## Per second specs

Machine: Apple M5 (10 cores), macOS, Apple clang 21, `-O2`.

Number = 64 byte objects pushed **and** popped in 1 second (millions).

| run | spinlock | std::mutex | atomics |
|-----|----------|------------|---------|
| 1   | 4.73 M   | 11.11 M    | 12.22 M |
| 2   | 4.27 M   | 11.93 M    | 20.60 M |
| 3   | 4.15 M   | 4.10 M     | 28.52 M |
| 4   | 2.24 M   | 4.17 M     | 40.20 M |
| 5   | 3.96 M   | 6.17 M     | 60.43 M |
| **median** | **~4.1 M/sec** | **~6.2 M/sec** | **~28.5 M/sec** |

In bytes: atomics median is about 28.5M * 64 B = ~1.8 GB/sec through the queue.

Context switches and cpu time (from `getrusage`, no `perf` on mac), typical run:

| version    | cpu time (2 threads, 1 sec) | context switches |
|------------|-----------------------------|------------------|
| spinlock   | ~1.2 - 2.0 s                | ~2k - 10k        |
| std::mutex | ~0.75 - 1.5 s               | ~45k - 130k      |
| atomics    | ~1.0 - 2.0 s                | ~2k - 18k        |

## Observations

- Lock free with atomics is the fastest by a lot, about 5-7x the spinlock.
- My spinlock was actually **slower than std::mutex** on mac. Both threads keep hitting the
  same lock cache line, so it bounces between the cores all the time. std::mutex sleeps the
  thread instead (you can see this: much less cpu time but way more context switches).
- Removing false sharing (`alignas(64)`) alone was not enough, the big win was not reading
  the other thread's index on every push/pop.
- Numbers jump around a lot between runs. On mac you can't pin a thread to a core, so
  sometimes the threads land on efficiency cores or get moved around.
