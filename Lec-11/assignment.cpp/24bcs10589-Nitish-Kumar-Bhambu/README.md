# SPSC Queue Assignment

**Nitish Kumar Bhambu** | 24BCS10589

Implemented a basic SPSC (Single Producer Single Consumer) queue using a ring buffer. Tested with two locking mechanisms — spinlock (while loop with `atomic_flag`) and `std::mutex`.

Each object pushed/popped is **64 bytes** (one cache line).

## How to build & run

```bash
g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_bench
./spsc_bench
```

## Results

Ran on my laptop:
- **CPU**: 12th Gen Intel i5-12450H (8 cores, 12 threads)
- **RAM**: 16 GB DDR4
- **OS**: Linux (Fedora)
- **Compiler**: g++ with `-O2`

| Lock Type | Throughput (ops/sec) | ~MB/s |
|---|---|---|
| Spinlock (`atomic_flag`) | ~6.3 million | ~385 |
| `std::mutex` | ~6.7 million | ~413 |

Numbers vary between runs. Mutex is more consistent, spinlock fluctuates a lot (anywhere from 2-6M depending on scheduling). With just 1 producer + 1 consumer there isn't much contention so mutex does surprisingly well here.

Queue capacity used: 1024 slots.
