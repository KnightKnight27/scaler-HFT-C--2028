# Lec-11 Assignment: SPSC Queue with Locks

- name: "Ujjawal Prabhat"
- email: "ujjawal.24bcs10267@sst.scaler.com"
- roll_no: "24BCS10267"

## What it does

`spsc_queue.cpp` implements a single-producer / single-consumer queue as a
fixed-capacity ring buffer guarded by a lock, and measures how many **64-byte
objects** can be pushed by one thread and popped by another in **1 second**.

- `SPSCQueue<T, Capacity, Lock>` is templated on the lock, so the same queue is benchmarked with
  - `std::mutex`
  - `SpinLock`: a test-and-test-and-set spinlock (`while` loop on an `std::atomic<bool>`)
    with exponential backoff using the CPU `yield`/`pause` hint.
- Capacity (1024) is a power of two, so slot index = `counter & (Capacity - 1)`. The buffer
  is allocated once up front, so nothing is allocated on the hot path.
- `Message` is exactly 64 bytes (`static_assert`): a sequence number plus 7 payload words.
- A producer thread and a consumer thread start together on a flag. The main thread
  sleeps for 1 s, sets `stop`, then calls `t1.join()` and `t2.join()`.
- **Throughput = objects popped by the consumer / measured elapsed time.** Failed attempts
  (queue full or queue empty) are not counted.
- **Correctness check:** the consumer checks that every message arrives with the next
  expected sequence number. Each run prints `FIFO ok`, or `FIFO BROKEN` if a message was
  lost, duplicated or reordered.

## Build & run

```bash
clang++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

The ThreadSanitizer build (`-fsanitize=thread`) reports no data races.

## Machine specs

| | |
|---|---|
| CPU | Apple M5 (arm64), 10 cores (4 performance + 6 efficiency) |
| Cache line | 128 bytes |
| RAM | 24 GB |
| OS | macOS 27.0.1 |
| Compiler | Apple clang 21.0.0, `-std=c++20 -O2` |

## Results (per second specs)

Capacity 1024, 5 runs of 1 second each.

### std::mutex

| Run | Objects | Elapsed (s) | Objects/s | MB/s |
|---|---:|---:|---:|---:|
| 1 | 22,590,237 | 1.000954 | 22,568,705 | 1444.4 |
| 2 | 24,005,160 | 1.001027 | 23,980,534 | 1534.8 |
| 3 | 24,346,073 | 1.005009 | 24,224,733 | 1550.4 |
| 4 | 23,029,123 | 1.005011 | 22,914,305 | 1466.5 |
| 5 | 23,528,474 | 1.005009 | 23,411,218 | 1498.3 |

**Median: ~23.4 million 64-byte objects/second (~1.5 GB/s)**

### SpinLock (with backoff)

| Run | Objects | Elapsed (s) | Objects/s | MB/s |
|---|---:|---:|---:|---:|
| 1 | 14,190,482 | 1.005015 | 14,119,669 | 903.7 |
| 2 | 12,130,272 | 1.004787 | 12,072,477 | 772.6 |
| 3 | 11,599,228 | 1.005011 | 11,541,400 | 738.6 |
| 4 | 11,958,257 | 1.005008 | 11,898,667 | 761.5 |
| 5 | 11,591,649 | 1.000113 | 11,590,338 | 741.8 |

**Median: ~11.9 million 64-byte objects/second (~0.76 GB/s)**

Every run printed `FIFO ok`. Over repeated runs of the benchmark, the median was usually
**20–24 M/s for the mutex** and **11–14 M/s for the spinlock**. A few individual runs collapsed
to under 1 M/s, most likely because the OS scheduled a thread badly or the machine was busy.
The median filters these out.

## Observations

1. **On this machine `std::mutex` is ~2x faster than the spinlock.** Running each lock alone
   under `/usr/bin/time -l`:

   | | median | user CPU | sys CPU | context switches |
   |---|---:|---:|---:|---:|
   | `std::mutex` | 23.5 M/s | 3.5 s | 4.56 s | 2,179,097 |
   | `SpinLock` | 11.2 M/s | 10.0 s | 0.02 s | 1,586 |

   A contended `std::mutex` on macOS puts the waiting thread to sleep in the kernel (~430 K
   times/s here). Meanwhile the other thread gets uncontended push/pop calls in a row: about
   54 objects per context switch. The spinlock never sleeps, which keeps both cores busy, but
   the lock's cache line moves between cores much more often.
2. **Backoff matters for the spinlock.** With a single `yield` per spin iteration (no
   backoff), the same code gets only **~3–4.5 M objects/s**. An instrumented run of that
   version showed **~40 M failed attempts (queue full/empty) for ~1.6 M successful
   transfers**, so the threads were mostly fighting over the lock. Exponential backoff
   (1 → 1024 `yield`s) gives the **~12–13 M objects/s** shown above, about 3–4x faster.
3. **Cache-line alignment helps the spinlock.** Without `alignas(128)` on the lock and the
   indices, the spinlock dropped to **~7.4–8.6 M objects/s** (vs ~12–13 M with it), in every
   A/B session. A spinning waiter reads the lock's cache line. If head/tail sit on the same
   line, the waiter keeps pulling it away from the thread that holds the lock while that
   thread is updating them. For `std::mutex`, the difference was within run-to-run noise.
4. **Locks are the bottleneck, not copying.** At ~23 M/s, the queue moves one object every
   ~43 ns. A 64-byte copy is only a few instructions, so most of that time goes to lock
   handoff and moving cache lines between cores. A lock-free SPSC queue (atomic head/tail on
   separate cache lines, no lock) would remove this cost.
