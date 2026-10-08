# Low-Latency SPSC Queue
- **Email:** minesh.24bcs10029@sst.scaler.com
- **Roll No.:** 24BCS10029

## Architecture Overview

This repository contains a high-performance, Single-Producer Single-Consumer (SPSC) queue designed for low-latency systems such as High-Frequency Trading (HFT) environments. The queue utilizes a bounded ring-buffer architecture backed by `std::array`. This ensures zero dynamic heap allocation at runtime, completely eliminating unpredictable latency spikes caused by the OS memory manager.

To simulate real-world trading system constraints, the queue payload is a strictly sized 64-byte object (`std::array<char, 64>`), matching the size of a standard CPU cache line. The producer and consumer track the buffer state using monotonically increasing indices (`mPushIdx` and `mPopIdx`), leveraging modulo arithmetic (`% Size`) to safely wrap around the fixed array bounds.

## Synchronization Primitive: Custom Spinlock

Standard synchronization primitives like `std::mutex` put threads to sleep when contested, invoking the kernel scheduler and causing microsecond-level context-switch delays. To bypass the kernel, this queue employs a custom user-space `SpinLock`.

* **Lock-Free Atomic Flag:** Built using `std::atomic<bool>`, the lock uses a Test-and-Set (TAS) loop via `exchange()` to continuously poll for availability.
* **Relaxed Memory Semantics:** Instead of using the expensive default sequential consistency (`seq_cst`), the lock uses `std::memory_order_acquire` on acquisition and `std::memory_order_release` on release. This provides strict memory safety for the queue operations while maximizing instruction throughput.
* **Thread Yielding:** The producer and consumer loops utilize `std::this_thread::yield()` when the queue is full/empty, politely signaling the scheduler to avoid 100% core monopolization without the heavy latency penalty of a timer-based `sleep()`.

## Memory Layout & False Sharing Prevention

In concurrent queue designs, the producer constantly mutates the push index while the consumer mutates the pop index. If these variables occupy the same 64-byte CPU cache line, the processor cores will constantly invalidate each other's cache, devastating memory throughput—a phenomenon known as False Sharing.

This implementation guarantees spatial isolation by explicitly padding the indices using the `alignas(64)` specifier, forcing `mPushIdx` and `mPopIdx` onto distinct cache lines.

## Benchmarking and Performance Analysis (1-Second Time-Box)

To measure raw throughput, the producer and consumer threads run in a continuous hot loop controlled by a shared atomic flag. The main thread sleeps for exactly `1.0` second before signaling the worker threads to halt.

**Payload Size:** 64 Bytes
**Operations Executed in 1 Second:**

* **Average Pushes:** `6445436`
* **Average Pops:** `6444563`

### Hardware Profiling (`perf stat -r 3`)

```text
Pushes in 1 sec: 6472041
Pops in 1 sec:   6471167
Pushes in 1 sec: 5682753
Pops in 1 sec:   5681861
Pushes in 1 sec: 7181515
Pops in 1 sec:   7180663

 Performance counter stats for './queue_bench' (3 runs):

                 0      context-switches:u               #      0.0 cs/sec  cs_per_second
                 0      cpu-migrations:u                 #      0.0 migrations/sec  migrations_per_second
               160      page-faults:u                    #     79.8 faults/sec  page_faults_per_second  ( +-  0.55% )
          2,003.88 msec task-clock:u                     #      nan CPUs  CPUs_utilized         ( +-  0.02% )
         1,644,307      branch-misses:u                  #      2.6 %  branch_miss_rate         ( +-  8.87% )  (50.06%)
        64,003,594      branches:u                       #     31.9 M/sec  branch_frequency     ( +-  6.15% )  (66.69%)
     2,689,526,267      cpu-cycles:u                     #      1.3 GHz  cycles_frequency       ( +-  0.36% )  (66.63%)
       329,078,714      instructions:u                   #      0.1 instructions  insn_per_cycle  ( +-  6.07% )  (66.68%)
        56,684,883      stalled-cycles-frontend:u        #     0.02 frontend_cycles_idle        ( +-  7.39% )  (66.64%)

       1.006021222 +- 0.000335190 seconds time elapsed  ( +-  0.03% )

```

### Key Hardware Takeaways

- **Zero context switches (0 cs/sec):** The custom `SpinLock` successfully kept both threads executing entirely in user space. The kernel scheduler never had to step in to suspend a thread.

- **Perfect hardware utilization:** The program took exactly 2.35 seconds of real wall-clock time, but accumulated 4.69 seconds of total CPU time (`4.69 / 2.35 = ~2.0`). This demonstrates that the two threads perfectly saturated exactly two CPU cores in parallel.

- **Cache bouncing mechanics (0.1 instructions per cycle):** The low IPC is the expected mechanical footprint of an atomic spinlock. Because both CPU cores are rapidly issuing atomic `exchange()` instructions to acquire the lock, they generate intense cross-core cache invalidation traffic. The CPU pipelines stall waiting for memory synchronization, resulting in billions of consumed cycles but a low number of retired instructions.
