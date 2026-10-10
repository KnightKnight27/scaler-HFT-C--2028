email: "ratnesh.24bcs10413@sst.scaler.com"

roll_no: "24bcs10413"

# Lec-11 Assignment: SPSC Queue with Locks

One producer thread pushes 64-byte objects into a queue and one consumer thread pops them for one second. The program reports how many objects got through per second.

**Answer (with locks, threads on separate cores):** about **12.2 M objects/s with `std::mutex`** (0.78 GB/s) and about **9.0 M objects/s with a spinlock** (0.58 GB/s).

## What's in `spsc_queue.cpp`

| Piece | What it does | Lecture it comes from |
| :--- | :--- | :--- |
| `Order` | The 64-byte object: the order struct from the capstone order book (`id`, `side`, `priceTicks`, `qty`, …), `alignas(64)` and `static_assert`ed to one cache line. | Capstone |
| `RingStorage<T>` | The memory pool. One slab of raw slots allocated once with `::operator new`; objects are placement-new'd on push and destroyed with `~T()` on pop, so the hot path never allocates. Capacity is a power of 2 and indices only grow, so `idx & mask` replaces `idx % size`. | Lec-6 (heap memory), Lec-10 |
| `TasSpinLock` | A `while` loop on `atomic_flag::test_and_set`. | Lec-11 |
| `TtasSpinLock` | Test-and-test-and-set: waits on a plain load with `pause`, then tries `exchange`. | Lec-11 |
| `LockedSpscQueue<T, Lock>` | A ring buffer where one lock guards both indices. Used with `TasSpinLock`, `TtasSpinLock` and `std::mutex`. `pop(T&)` moves out into the caller's object instead of returning a copy. | Lec-9 (move semantics), Lec-10, Lec-11 |
| `LockFreeSpscQueue<T, CacheIndices>` | Bonus: no lock. Head and tail are atomics on separate cache lines, published with release and read with acquire. With `CacheIndices = true`, each side keeps a private copy of the other side's index. | Lec-12 |

Every run checks that the consumer receives ids `0, 1, 2, …` in order (the FIFO column). Besides throughput, the program prints:

- **empty %**: pop attempts that found the queue empty.
- **full %**: push attempts that found it full.

These show which side is the bottleneck.

The code also ran clean under ThreadSanitizer (`-fsanitize=thread`).

## Machine

- AMD Ryzen 5 5600H (Zen 3, 6 cores / 12 threads, 512 KiB L2 per core, 16 MiB shared L3)
- Ubuntu 24.04.4 LTS, Linux 7.0.0
- g++ 13.3.0, `-std=c++20 -O3 -march=native -pthread`
- Queue capacity 4096 slots (256 KiB). Each number is the median of 5 one-second runs.

## Results (objects popped per second)

| Queue | Not pinned | Separate cores (CPU 0 → 2) | SMT siblings (CPU 0 → 1) |
| :--- | ---: | ---: | ---: |
| TAS spinlock | 4.34 M/s | 9.00 M/s | 17.33 M/s |
| TTAS spinlock | 4.21 M/s | 8.38 M/s | 14.44 M/s |
| `std::mutex` | 11.51 M/s | **12.19 M/s** | 5.91 M/s |
| lock-free (bonus) | 20.84 M/s | 18.62 M/s | 45.75 M/s |
| lock-free + cached indices (bonus) | 20.83 M/s | **20.35 M/s** | **56.03 M/s** |

Pop-empty and push-full rates with the threads on separate cores:

| Queue | empty % | full % |
| :--- | ---: | ---: |
| TAS spinlock | 0.3 | 33.0 |
| TTAS spinlock | 2.1 | 54.6 |
| `std::mutex` | 13.9 | 8.7 |
| lock-free | 78.5 | 14.4 |
| lock-free + cached indices | 88.2 | 0.0 |

All runs passed the FIFO check.

## Observations

- **`std::mutex` beats both spinlocks across cores.** With the spinlocks, the push-full rate is 33–55%. The consumer can't keep up, so the producer keeps taking the lock only to find the queue full. Each of those failed pushes still drags the lock's cache line to the producer's core and delays the consumer. The mutex makes a contended thread sleep instead of retrying, which wastes fewer lock acquisitions, so it ends up ahead.
- **TTAS is not faster than TAS with only two threads.** TTAS is meant to cut the write traffic from *many* waiters spinning on one lock. With a single waiter there is little traffic to save, and the extra load plus `pause` in the wait loop only delays noticing that the lock was released.
- **Most of the cost is cache lines moving between cores.** When both threads run on SMT siblings, they share L1 and L2. The lock-free queue then goes from 20 M/s to 56 M/s and the spinlocks roughly double. `std::mutex` is the exception and gets slower. My guess is the futex sleep/wake cost no longer overlaps with useful work on the other core, but I didn't measure this.
- **Lock-free is the fastest, and here it is limited by the producer.** The consumer finds the queue empty 78–88% of the time, so it spends most of its time waiting for the next element. Cached indices help mostly on the producer side: the push-full rate drops from 14.4% to 0, because the producer no longer reads the consumer's line on every push. The consumer's cached tail is almost always stale while the queue is near-empty.
- **Pinning matters for spinlocks.** Without pinning, the spinlock numbers halve. The scheduler can move a thread while it holds the lock, or put both threads on one core, and then the other thread spins for a whole time slice. For latency-sensitive code, the producer and consumer should be pinned to separate physical cores. The SMT-sibling column is an experiment, not a setup you would run in production.

## Build and run

```bash
cd Lec-11/assignment.cpp/24bcs10413-Ratnesh-Vaibhav
g++ -std=c++20 -O3 -march=native -Wall -Wextra -pthread spsc_queue.cpp -o spsc_queue

./spsc_queue            # 1 s x 5 trials, OS picks the cores
./spsc_queue 1 5 0 2    # pin producer to CPU 0, consumer to CPU 2
```

Arguments: `[seconds] [trials] [producer_cpu] [consumer_cpu]`. A CPU of `-1` means not pinned. Pinning works on Linux only. On Linux, `cat /sys/devices/system/cpu/cpu0/topology/thread_siblings_list` shows which CPUs are SMT siblings.
