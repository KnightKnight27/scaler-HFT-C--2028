# SPSC Queue with Locks (Spinlock vs std::mutex)

- name: Ankit Kumar
- email: ankit.24bcs10189@sst.scaler.com
- roll_no:24bcs10189

**Assignment:** write an SPSC (single producer, single consumer) queue that uses a lock (a spinlock `while` loop or `std::mutex`). One producer thread pushes 64-byte objects and one consumer thread pops them (`t1.join()`, `t2.join()`). Measure how many 64-byte objects can be pushed and popped in 1 second.

Everything is in one file, [`spsc_queue.cpp`](spsc_queue.cpp). The queue is a template on the lock type, `SPSC<T, Lock>`, so the spinlock and `std::mutex` versions run exactly the same queue code and only the lock changes.

## Results (TL;DR)

64-byte objects moved from producer to consumer in 1 second, averaged over 6 rounds at `-O0`:

| Lock       | objects / sec  | throughput   |
|------------|----------------|--------------|
| SpinLock   | **~3.3 million** | ~201 MB/s |
| std::mutex | **~9.4 million** | ~576 MB/s |

(MB here means 2^20 bytes.) On my Windows machine `std::mutex` was **~2.9x faster** than the spinlock. The explanation is further down. In short, the spinlock is unfair: the producer keeps getting the lock back and wastes it finding the queue full, while the consumer starves.

## What I made

- **Object:** `struct alignas(64) Order { long long id; char data[56]; }`, which is exactly 64 bytes (one cache line). A `static_assert` checks the size.
- **Memory pool:** the ring buffer is raw memory allocated **once** in the constructor with aligned `::operator new`. `push`/`pop` never allocate. `push` builds the object in its slot with placement `new`, and `pop` moves it out and calls `~T()` on the slot. The destructor destroys anything still left in the queue and then frees the pool.
- **Ring buffer, size a power of 2:** I index with `idx & mMask` instead of `idx % size`, because modulo is a slow division. The constructor aborts if the size isn't a power of 2.
- **Indexes only go up:** `mPushIdx` and `mPopIdx` are never reset or wrapped. Full means `push - pop == size` and empty means `push == pop`, as in the class SPSC. They are plain `size_t` because the lock already protects them.
- **`pop(T& val)`:** writes straight into the caller's object, so the return value is never copied.
- **`[[unlikely]]`** on the full and empty branches, since in the normal case the queue is neither (the pipeline stall topic from class).
- Copy and move constructors and assignments are deleted, because a queue shared by two threads shouldn't be copied.

### SpinLock

```cpp
void lock() {
  bool expected = false;
  while (!mFlag.compare_exchange_weak(expected, true, acquire, relaxed)) {
    expected = false;                 // CAS wrote `true` into expected on failure
    while (mFlag.load(relaxed))       // test-and-test-and-set: only READ while it's taken
      _mm_pause();                    // tell the CPU we are spinning
  }
}
void unlock() { mFlag.store(false, release); }
```

- `compare_exchange_weak` means "if the flag is `false`, make it `true`" in one atomic step, so only one thread can take the lock. `weak` can fail spuriously, but we're already in a loop, so that's fine.
- While someone else holds the lock, I only *load* the flag instead of hammering it with CAS. A load can share the cache line, while a CAS needs it exclusive and would keep stealing it from the owner.
- `acquire` on lock and `release` on unlock stop the reads and writes inside the critical section from being reordered outside the lock.
- It never sleeps.

### std::mutex

It's the same `SPSC` with `Lock = std::mutex`. When the mutex is contended, the waiting thread can block in the kernel instead of spinning.

## How I measured

- `t1` = producer: pushes `Order`s with ids 0, 1, 2, … and counts successful pushes plus failed pushes (queue full).
- `t2` = consumer: pops, **checks that the ids come out in order**, and counts successful pops plus failed pops (queue empty).
- Both threads wait on a `start` flag so they begin together. `main` sleeps 1 second, sets `stop`, then does `t1.join()` and `t2.join()`.
- Each thread counts in its own local variables and writes the totals out only once at the end, so the counters don't cause false sharing.
- Elapsed time is measured with `steady_clock`, and objects/sec = popped / elapsed (usually 1.00–1.015 s).
- Rounds alternate (spin, mutex, spin, mutex, …) so both locks see the same machine state.
- Queue capacity is 1024 slots × 64 B = 64 KB.

### Build and run

```bash
# Windows (MSYS2 UCRT64 shell) or Linux
g++ -O0 -std=c++20 -pthread spsc_queue.cpp -o spsc
./spsc            # 5 rounds of both locks
./spsc 6          # 6 rounds
./spsc 1 spin     # only the spinlock  (./spsc 1 mutex for only std::mutex)
```

`-O0` matches what we use in class. I also show `-O2` below. It also compiles with `-std=c++17` (GCC accepts `[[unlikely]]` as an extension there).

## My system

- CPU: 13th Gen Intel Core i5-13450HX (10 cores = 6 P-cores + 4 E-cores, 16 threads)
- RAM: 16 GB
- OS: Windows 11 Home (build 26300)
- Compiler: g++ 16.1.0 (MSYS2 UCRT64, winpthreads)

## Full results

### `-O0`, 6 rounds (`./spsc 6`)

| Round | Spinlock popped / 1 s | Spinlock M obj/s | std::mutex popped / 1 s | std::mutex M obj/s |
|------:|----------------------:|-----------------:|------------------------:|-------------------:|
| 1 | 3,150,030 | 3.13 | 8,523,143  | 8.52  |
| 2 | 3,631,123 | 3.59 | 9,034,047  | 9.00  |
| 3 | 3,568,485 | 3.52 | 9,807,749  | 9.72  |
| 4 | 3,471,809 | 3.43 | 9,547,749  | 9.44  |
| 5 | 3,258,395 | 3.22 | 9,782,235  | 9.65  |
| 6 | 2,896,233 | 2.88 | 10,443,044 | 10.29 |
| **Avg** | | **3.29** | | **9.44** |

The order check passed in every round for both locks. Pushed minus popped is always ≤ 1024, which is just what was left in the queue when we stopped.

### Failed push / pop (average per second, same run)

| Lock | successful pops | push found queue **full** | pop found queue **empty** | total lock acquisitions |
|------|----------------:|--------------------------:|--------------------------:|------------------------:|
| SpinLock   | 3.29 M | **7.14 M** | 0.35 M | ~14.1 M |
| std::mutex | 9.44 M | 3.16 M | 4.01 M | ~26.0 M |

### CPU time (1 round, process run alone, measured from PowerShell)

On Windows there's no `perf stat`, so I read the process's user and kernel CPU time (`UserProcessorTime` / `PrivilegedProcessorTime`):

| Lock | Run | M obj/s | user CPU | kernel CPU | total CPU in ~1 s |
|------|----:|--------:|---------:|-----------:|------------------:|
| SpinLock   | 1 | 3.39 | 1.891 s | 0.078 s | 1.97 s |
| SpinLock   | 2 | 5.36 | 1.906 s | 0.125 s | 2.03 s |
| SpinLock   | 3 | 4.02 | 1.875 s | 0.141 s | 2.02 s |
| std::mutex | 1 | 9.16 | 0.563 s | 0.750 s | 1.31 s |
| std::mutex | 2 | 8.60 | 0.484 s | 0.859 s | 1.34 s |
| std::mutex | 3 | 8.50 | 0.719 s | 0.688 s | 1.41 s |

### `-O2`, 6 rounds (for comparison)

| Round | 1 | 2 | 3 | 4 | 5 | 6 | **Avg** |
|-------|---|---|---|---|---|---|---------|
| Spinlock (M obj/s)   | 6.59 | 5.80 | 6.41 | 6.63 | 6.02 | 6.10 | **6.26** (~382 MB/s) |
| std::mutex (M obj/s) | 13.25 | 10.50 | 13.29 | 9.52 | 9.48 | 9.93 | **10.99** (~671 MB/s) |

### Spinlock variant I also tried

A plain CAS loop with no inner read loop, like the class hint. At `-O0` it gave 3.50 / 3.53 / 3.80 M obj/s (avg **3.61 M**; std::mutex got 9.02 M in the same run), about the same as my test-and-test-and-set version. So the gap isn't caused by the spin-wait details.

## What I noticed / why std::mutex won here

1. **The spinlock is unfair, and the producer wins.** Look at the "full" column: with the spinlock, the producer took the lock **~7.1 M times per second just to find the queue full**, while the consumer only got ~3.3 M successful pops. When a thread unlocks, the flag's cache line is still in its own L1, so if it calls `lock()` again right away its CAS succeeds before the other core even sees the release. The producer's loop is shorter (a failed push is just lock → compare → unlock), so it keeps re-grabbing the lock. The queue sits full and the consumer, which does the real work (copy out, destructor, order check), starves.
2. **Cache-line ping-pong.** Every time the lock really moves between the two cores, the flag's cache line and the index cache line (the lock and both indexes sit next to each other in the queue object) have to move between the cores' L1s (MESI). The spinlock does this all the time, because both threads are hammering it.
3. **The mutex batches the work.** When `std::mutex` is contended, the losing thread goes to sleep in the kernel. The kernel time column shows it: about 0.7–0.86 s for mutex vs ~0.1 s for spin. While one side sleeps, the other gets the lock many times in a row with the cache lines staying hot in *its* L1. So the producer fills a burst, then the consumer drains a burst. Both "full" (3.2 M) and "empty" (4.0 M) are large for the mutex, which fits the queue swinging between full and empty in bursts. Fewer cache-line transfers per object means more objects per second: ~26 M lock acquisitions per second vs ~14 M for the spinlock.
4. **CPU cost.** The spinlock burns ~2 full CPU-seconds per second (both threads at 100%, almost all user time) even while waiting. The mutex used only ~1.3–1.4 CPU-seconds and moved ~2.9x more objects.
5. **`-O0` vs `-O2`.** At `-O0` every `std::atomic` member call is a real function call that isn't inlined, so `lock()`/`unlock()` and the critical section are slow. With `-O2` the spinlock almost doubles (3.3 → 6.3 M/s). The mutex gains less (9.4 → 11.0 M/s), since its internals are already optimized library code. The mutex is still ahead.
6. **This is different from Linux results.** Other submissions measured on Linux/WSL found the spinlock faster than `std::mutex`. On Windows, `std::mutex` from MinGW goes through winpthreads and the Windows scheduler, and blocking there clearly helps by forcing the batching described above. So the result depends on the OS, the mutex implementation and the core layout, and you have to measure on the target machine.
7. **Big run-to-run variance.** The spinlock went from 2.9 M to 5.4 M across runs. This CPU has P-cores and E-cores, and which cores Windows puts `t1` and `t2` on (and what else is running on the laptop) changes the cache-transfer cost a lot.

## How to make it faster (next step)

The lock is the bottleneck: only one of producer or consumer can make progress at a time. For SPSC we don't need a lock at all. As in the lock-free SPSC from class, we can make `mPushIdx` / `mPopIdx` `std::atomic<size_t>` with `alignas(64)` (separate cache lines), have the producer write only `mPushIdx` (release) and the consumer write only `mPopIdx` (release), and cache the other side's index locally. Then producer and consumer run in parallel and never wait for each other.
