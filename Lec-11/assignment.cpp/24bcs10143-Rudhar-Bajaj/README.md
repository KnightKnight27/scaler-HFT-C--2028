# Lec-11: SPSC queue with locks, 64-byte objects per second

email: "rudhar.24bcs10143@sst.scaler.com"

roll_no: "10143"

Name: Rudhar Bajaj

## What it does

[`spsc_queue.cpp`](spsc_queue.cpp) has one **producer** thread pushing 64-byte objects into a bounded queue and one **consumer** thread popping them. Both threads start together, run for **1 second**, and stop. Then `t1.join()` / `t2.join()` run, and the program prints how many objects made it through.

- **Object:** `struct alignas(64) Msg { uint64_t seq; uint64_t payload[7]; }`. `static_assert` checks that it is exactly 64 bytes, one cache line.
- **Queue:** a ring buffer over a preallocated `std::vector<Msg>`, acting as a fixed memory pool of slots. Push and pop only copy 64 bytes into or out of a slot and never call `new` or `delete`.
- **Locks.** The queue is templated on the lock type, so the same code is benchmarked with:
  - `SpinLock`: a `while` loop on `std::atomic<bool>` (test-and-test-and-set, with a `yield`/`pause` hint while spinning)
  - `std::mutex`
  - a **lock-free** SPSC queue (atomic head/tail with acquire/release), as a baseline only
- **Correctness check:** the consumer checks that every popped `seq` is exactly the next expected number, so nothing is lost, duplicated or reordered. Any mismatch prints `order=BROKEN`.
- Each configuration runs 5 trials of 1 s each. The table shows min / median / max.

## Build & run

```bash
clang++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc     # or g++
./spsc        # 5 trials per config (default)
./spsc 10     # 10 trials per config
```

## Per-second specs (my machine)

Apple M5 Pro (15 cores), macOS, Apple clang 21, `-O2`. These are objects successfully **popped** by the consumer in 1 second (each push is matched by a pop):

| Lock | Queue capacity | Min | **Median** | Max | Bandwidth (median) |
|---|---|---|---|---|---|
| Spinlock (while loop) | 1024 | 10.02 M/s | **10.18 M/s** | 10.33 M/s | ~651 MB/s |
| `std::mutex` | 1024 | 12.65 M/s | **13.00 M/s** | 13.09 M/s | ~832 MB/s |
| Lock-free (baseline) | 1024 | 17.18 M/s | **17.69 M/s** | 19.41 M/s | ~1132 MB/s |
| Spinlock (while loop) | 65536 | 10.00 M/s | **10.75 M/s** | 10.91 M/s | ~688 MB/s |
| `std::mutex` | 65536 | 15.19 M/s | **15.46 M/s** | 15.62 M/s | ~990 MB/s |
| Lock-free (baseline) | 65536 | 13.86 M/s | **14.88 M/s** | 15.05 M/s | ~953 MB/s |

**With a lock, about 10–15 million 64-byte objects are pushed and popped per second (~0.65–1 GB/s).** That is roughly 65–100 ns per object end to end.

## Observations

- **The spinlock is slower than `std::mutex` here.** With exactly two threads hammering one lock, both cores keep doing `exchange` on the same cache line, so the line ping-pongs between cores on almost every operation. macOS's `std::mutex` spins briefly and then parks the waiter, so one side often gets several uncontended push/pop ops in a row.
- **The lock is the bottleneck, not the copy.** Copying 64 bytes takes a few ns. Most of the ~70–100 ns per object goes to moving the lock's cache line (and the slot's) between the two cores.
- **Lock-free is not dramatically faster in this setup** because the consumer keeps up with the producer, so the queue is almost always nearly empty. Every message then costs a cross-core cache miss on the slot plus the tail index. Batching (pushing or popping several objects per lock/index update) would be the next step to raise throughput.
- **False sharing matters.** In my first version, the `pushed`, `popped` and `stop` counters sat on the same stack cache line, and the producer wrote `pushed` on every push. Moving the counters into thread-local variables and `alignas(64)`-padding the shared flags raised the lock-free numbers from ~14 M/s to ~18 M/s at cap 1024.
- Numbers vary run to run (±5–10%) depending on whether the OS puts the threads on performance or efficiency cores.
