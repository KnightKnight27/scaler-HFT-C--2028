# Lec-11 Assignment: SPSC Queue with Locks (64-byte objects / second)

## Submission Details

```yaml
name: "Subhan Rahiman"
email: "subhan.24bcs10095@sst.scaler.com"
roll_no: "24BCS10095"
```

## Summary

A single-producer / single-consumer queue guarded by a lock, used to measure how many 64-byte objects
can be pushed and popped in 1 second. Headline numbers (median of 5 runs, Apple M3 Pro):

| `std::mutex` | Spinlock | Lock-free (baseline) |
|---|---|---|
| ~31.7 M objects/s | ~8.4 M objects/s | ~105 M objects/s |

Single-producer / single-consumer queue, one producer thread and one consumer
thread (`t1.join()`, `t2.join()`), each run for 1 second. Source: [`spsc_queue.cpp`](./spsc_queue.cpp).

## Design

- **Memory pool:** one ring buffer of 1024 pre-allocated 64-byte slots, allocated once in the
  constructor. No `new`/`malloc` on the push/pop path.
- **Object:** `struct alignas(64) Obj { uint64_t seq; uint64_t filler[7]; }`, exactly one cache line
  (`static_assert(sizeof(Obj) == 64)`).
- **Same queue, three sync policies:**
  1. `std::mutex`
  2. `SpinLock` (`std::atomic_flag` busy-wait loop with a `yield` hint)
  3. No lock (lock-free SPSC with acquire/release atomics): **baseline only**, to show what the locks cost.
- **Correctness check:** every object carries a sequence number. The consumer verifies FIFO order and the
  program reports the error count (0 in every run).

## How to run

```bash
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue        # 3 runs per variant
./spsc_queue 5      # 5 runs per variant
```

## Machine

| | |
|---|---|
| CPU | Apple M3 Pro (11 cores: 5 performance + 6 efficiency) |
| RAM | 18 GB |
| OS | macOS 27.0.1 |
| Compiler | Apple clang 21.0.0, `-O2 -std=c++17` |

## Results (64-byte objects pushed + popped in 1 second, 5 runs each)

| Variant | Median objects/sec | Best objects/sec | Median payload throughput |
|---|---|---|---|
| `std::mutex` | **31.7 million** | 32.5 million | 2.03 GB/s |
| `SpinLock` | **8.4 million** | 9.2 million | 0.54 GB/s |
| Lock-free (baseline) | **105.2 million** | 106.6 million | 6.73 GB/s |

Raw per-run output:

```text
sizeof(Obj) = 64 bytes, queue capacity = 1024 slots, hw threads = 11

== std::mutex SPSC queue ==
  run 1: pushed=32631737 popped=32631737  -> 32.47 M objs/s  (2.08 GB/s of 64B payload)
  run 2: pushed=32486714 popped=32486017  -> 32.32 M objs/s  (2.07 GB/s of 64B payload)
  run 3: pushed=31085990 popped=31085134  -> 31.08 M objs/s  (1.99 GB/s of 64B payload)
  run 4: pushed=31827815 popped=31827248  -> 31.72 M objs/s  (2.03 GB/s of 64B payload)
  run 5: pushed=31799265 popped=31798654  -> 31.64 M objs/s  (2.02 GB/s of 64B payload)
  median: 31.72 M objs/s | best: 32.47 M objs/s | FIFO order errors: 0

== SpinLock SPSC queue ==
  run 1: pushed=9248066 popped=9247982  -> 9.20 M objs/s  (0.59 GB/s of 64B payload)
  run 2: pushed=8975038 popped=8974340  -> 8.93 M objs/s  (0.57 GB/s of 64B payload)
  run 3: pushed=8476819 popped=8476463  -> 8.43 M objs/s  (0.54 GB/s of 64B payload)
  run 4: pushed=7552356 popped=7551827  -> 7.54 M objs/s  (0.48 GB/s of 64B payload)
  run 5: pushed=7264201 popped=7264109  -> 7.23 M objs/s  (0.46 GB/s of 64B payload)
  median: 8.43 M objs/s | best: 9.20 M objs/s | FIFO order errors: 0

== Lock-free SPSC queue (baseline, no lock) ==
  run 1: pushed=105542560 popped=105542164  -> 105.23 M objs/s  (6.73 GB/s of 64B payload)
  run 2: pushed=102165033 popped=102165018  -> 101.65 M objs/s  (6.51 GB/s of 64B payload)
  run 3: pushed=102845485 popped=102845469  -> 102.33 M objs/s  (6.55 GB/s of 64B payload)
  run 4: pushed=106809431 popped=106809415  -> 106.28 M objs/s  (6.80 GB/s of 64B payload)
  run 5: pushed=107113178 popped=107113162  -> 106.58 M objs/s  (6.82 GB/s of 64B payload)
  median: 105.23 M objs/s | best: 106.58 M objs/s | FIFO order errors: 0
```

## Observations

- **Answer to the assignment question:** with a lock, this machine moves roughly **32 million** 64-byte objects/second
  through the queue with `std::mutex` and roughly **8 million**/second with a spinlock.
- **The spinlock lost to the mutex.** I did not profile this, so this is my likely explanation, not a
  measured fact: with two threads contending for one lock on every call, the waiting thread keeps pulling the
  lock's cache line away from the thread holding it, while `std::mutex` lets one thread keep the lock for
  longer stretches and do more work per hand-off. Spinlocks usually pay off when the critical section is tiny
  and contention is low, which is not the case here. The spinlock number also drifts down across runs
  (9.2 M to 7.2 M); I did not investigate why (core placement on a hybrid P/E-core chip is one suspect).
- **The lock costs ~3x to ~12x** compared with the lock-free queue (105 M/s), which is the point of the
  SPSC design: with exactly one producer and one consumer, head and tail each have a single writer,
  so no lock is needed at all.
- **Lock-free speed comes from two things:** head/tail on separate cache lines (no false sharing) and each
  side caching the other side's index so the shared atomic is read only when the queue looks full/empty.
- `pushed` can exceed `popped` by a few hundred objects: those are objects still sitting in the queue when
  the 1-second window ended. `popped` is what is reported.
- Numbers vary run to run (macOS scheduling, P vs E core placement). Treat them as orders of magnitude.
