# Lec-11 Assignment: Lock-based SPSC Queue

This program measures how many 64-byte objects one producer thread can push and one consumer thread can pop in 1 second, using a queue protected by a lock.

## What's in `spsc_queue.cpp`

- **`Msg`**: a `alignas(64)` struct with a 64-bit sequence number and 56 bytes of payload, so it is exactly 64 bytes, one cache line (checked with `static_assert`).
- **`SpinLock`**: a `while` loop on a `std::atomic_flag` (test-and-test-and-set).
- **`SPSCQueue<T, Lock>`**: a fixed-capacity ring buffer. All slots are allocated once in the constructor, which acts as the memory pool. `push` and `pop` copy into and out of those slots, so the hot path never calls `new` or `delete`. Every operation takes the lock.
- **`bench<Lock>()`**: starts a producer (`t1`) and a consumer (`t2`), lets them run for N seconds, stops the producer, lets the consumer drain the queue, and then calls `t1.join()` and `t2.join()`. The consumer also checks that every sequence number arrives in FIFO order and that pushed == popped.

The same queue runs twice: once with `SpinLock` and once with `std::mutex`.

## Build and run

```bash
g++ -O2 -std=c++20 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue              # capacity 1024, 1 second per run
./spsc_queue 65536 1      # [capacity] [seconds]
```

## Per-second results

Machine: Intel Core i7-13650HX (20 logical CPUs), Windows 11, g++ 16.2 (MSYS2 UCRT64), `-O2`.
Each run lasted 1 second. The counts are messages that made it all the way through (popped).

| Capacity | Lock        | msgs / sec (range over runs) | ≈ MB / sec  | ≈ ns / msg |
|---------:|-------------|------------------------------|-------------|------------|
| 1024     | SpinLock    | 4.5 M – 7.1 M                | 275 – 435   | 140 – 220  |
| 1024     | std::mutex  | 5.3 M – 7.7 M                | 325 – 470   | 130 – 190  |
| 65536    | SpinLock    | 6.9 M – 9.6 M                | 420 – 585   | 105 – 145  |
| 65536    | std::mutex  | 13.1 M – 13.7 M              | 800 – 840   | ~75        |

**Headline:** with a lock, this setup moves about **5–14 million 64-byte objects per second** (about 0.3–0.8 GB/s), depending on queue size and lock type.

Sample output:

```
[SpinLock]
  elapsed        : 1.00192 s
  pushed         : 6669729
  popped         : 6669729
  push (full)    : 2953050
  pop  (empty)   : 1248338
  msgs / sec     : 6656970
  MB / sec       : 406.309
  ns / msg       : 150.218
  FIFO order ok  : yes
```

## Observations

- **The single lock is the bottleneck.** Both threads fight over one lock (and one cache line), so each operation pays for that cache line moving between cores. That costs roughly 100–200 ns per message, not the few ns a plain copy of 64 bytes would take.
- **The spinlock is not automatically faster.** With a small queue the two locks are about equal. With a big queue `std::mutex` won clearly here. The spinlock lets one thread keep re-grabbing the lock and starves the other, and it wastes cycles on contention, while `std::mutex` (an SRW lock on Windows) backs off.
- **A bigger capacity helps.** With 1024 slots the producer often finds the queue full (the `push (full)` count), and the consumer often finds it empty. A bigger buffer absorbs these bursts.
- **Results vary a lot** from run to run (±30%) because of OS scheduling and P-core/E-core placement on this hybrid CPU. Pinning the threads to cores would make the numbers more stable.
- **Next step:** a lock-free SPSC queue (head and tail as `std::atomic` on separate cache lines, with acquire/release ordering) would remove the lock entirely and should be several times faster.
