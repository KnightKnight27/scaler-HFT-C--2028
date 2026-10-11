# Lec-11 — SPSC Queue (64-byte objects)

**Name:** Tanishka Mangure
**Roll:** 24bcs10264

Single-producer / single-consumer ring buffer over a preallocated memory pool
(capacity 8192, no allocation in the hot path). Payload is exactly 64 bytes
(one cache line). Benchmarks `std::mutex` vs spinlock (`atomic_flag` while-loop)
with one producer thread and one consumer thread (`t1.join()`, `t2.join()`)
over a 1-second window.

## Build & run

```bash
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_bench && ./spsc_bench
```

## Result

- CPU: AMD Ryzen 5 6600H, 12 threads, 14 GB RAM
- Compiler: g++ 16.2.1, `-O2 -std=c++17`
- `mutex:    pushed=~11.6M popped=~11.6M / sec`
- `spinlock: pushed=~7.2M popped=~7.2M / sec`
- Winner on this machine: **mutex**

Throughput varies per run and CPU. Re-run on your machine and compare.
At ~11.6M × 64 B this moves roughly 740 MB/s through the queue.
