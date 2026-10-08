# Low-Latency SPSC Queue
- **Email**: minesh.24bcs10029@sst.scaler.com
- **Roll No.:** 24BCS10029

## Architecture Overview

This repository contains a high-performance, Single-Producer Single-Consumer (SPSC) queue designed for low-latency systems such as High-Frequency Trading (HFT) environments.

The queue uses:

- A bounded ring-buffer architecture backed by `std::array`.
- Zero dynamic heap allocation at runtime, eliminating unpredictable latency spikes caused by the OS memory manager.
- Monotonically increasing producer and consumer indices (`mPushIdx` and `mPopIdx`).
- Modulo arithmetic (`% Size`) to safely wrap around the fixed array bounds.

## Synchronization Primitive: Custom Spinlock

Standard synchronization primitives such as `std::mutex` put threads to sleep when contested, invoking the kernel scheduler and causing microsecond-level context-switch delays. To bypass the kernel, this queue employs a custom user-space `SpinLock`.

The spinlock uses:

- **Lock-free atomic flag:** Built using `std::atomic<bool>`, the lock uses a Test-and-Set (TAS) loop via `exchange()` to continuously poll for availability.
- **Relaxed memory semantics:** Instead of using the expensive default sequential consistency (`seq_cst`), the lock uses `std::memory_order_acquire` on acquisition and `std::memory_order_release` on release. This provides strict memory safety for the queue operations while maximizing instruction throughput.
- **Thread yielding:** The producer and consumer loops utilize `std::this_thread::yield()` when the queue is full or empty. This politely signals the scheduler to avoid 100% core monopolization without the heavy latency penalty of a timer-based `sleep()`.

## Memory Layout and False Sharing Prevention

In concurrent queue designs, the producer constantly mutates the push index while the consumer mutates the pop index. If these variables occupy the same 64-byte CPU cache line, the processor cores will constantly invalidate each other's cache, devastating memory throughput—a phenomenon known as **false sharing**.

This implementation guarantees spatial isolation by explicitly padding the indices using the `alignas(64)` specifier, forcing `mPushIdx` and `mPopIdx` onto distinct cache lines.

## Benchmarking and Performance Analysis

The queue was benchmarked on a Linux environment by pushing and popping 10,000,000 integers between two threads compiled with `-O3` optimizations.

**Throughput:** 4.25 million operations per second

### Hardware Profiling (`perf stat`)

```text
Time: 2.34967 seconds
Throughput: 4.25591 Million Ops/sec

 Performance counter stats for './queue_bench':

                 0      context-switches:u               #      0.0 cs/sec  cs_per_second
                 0      cpu-migrations:u                 #      0.0 migrations/sec  migrations_per_second
               149      page-faults:u                    #     31.8 faults/sec  page_faults_per_second
          4,692.05 msec task-clock:u                     #      nan CPUs  CPUs_utilized
         8,083,955      branch-misses:u                  #      9.0 %  branch_miss_rate         (50.03%)
        89,246,091      branches:u                       #     19.0 M/sec  branch_frequency     (66.71%)
     6,389,141,825      cpu-cycles:u                     #      1.4 GHz  cycles_frequency       (66.67%)
       429,373,490      instructions:u                   #      0.1 instructions  insn_per_cycle  (66.63%)
       196,527,563      stalled-cycles-frontend:u        #     0.03 frontend_cycles_idle        (66.66%)

       2.354484196 seconds time elapsed
       4.537443000 seconds user
       0.028155000 seconds sys
```

## Key Hardware Takeaways

- **Zero context switches (0 cs/sec):** The custom `SpinLock` successfully kept both threads executing entirely in user space. The kernel scheduler never had to step in to suspend a thread.

- **Perfect hardware utilization:** The program took exactly 2.35 seconds of real wall-clock time, but accumulated 4.69 seconds of total CPU time (`4.69 / 2.35 = ~2.0`). This demonstrates that the two threads perfectly saturated exactly two CPU cores in parallel.

- **Cache bouncing mechanics (0.1 instructions per cycle):** The low IPC is the expected mechanical footprint of an atomic spinlock. Because both CPU cores are rapidly issuing atomic `exchange()` instructions to acquire the lock, they generate intense cross-core cache invalidation traffic. The CPU pipelines stall waiting for memory synchronization, resulting in billions of consumed cycles but a low number of retired instructions.
