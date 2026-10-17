# SPSC Queue — Performance Evolution

A C++ implementation and benchmark of a **Single-Producer Single-Consumer (SPSC) queue**, progressing from a mutex-based queue to an optimized lock-free implementation.

---

## Benchmark Results

![Benchmark Results](./screenshots/benchmark.png)

| Implementation | Synchronization | Operations | Throughput | Avg. Latency | Relative Performance |
|---|---|---:|---:|---:|---:|
| **LockSPSC** | `std::mutex` | 8,408,420 | 8.41 Mops/s | 118.96 ns | 1.0× |
| **SpinLockSPSC** | `atomic_flag` | 2,787,401 | 2.79 Mops/s | 358.80 ns | 0.33× |
| **Naive Lock-Free** | Acquire/Release Atomics | 13,260,223 | 13.26 Mops/s | 75.42 ns | 1.6× |
| **Optimized Lock-Free** | Cached Indices + Direct Stores | 86,734,818 | **86.72 Mops/s** | **11.53 ns** | **10.3×** |

**Mops/s** = Million operations per second.

---

## Implementations

### 1. Mutex-Based SPSC

Uses `std::mutex` to protect queue operations.

```text
Producer ──┐
           │
           ▼
       ┌────────┐
       │ Mutex  │
       └───┬────┘
           │
           ▼
       Queue Data
           ▲
           │
       ┌───┴────┐
       │ Mutex  │
       └────────┘
           ▲
           │
Consumer ──┘
```

Every push and pop requires acquiring and releasing the mutex.

---

### 2. Spinlock SPSC

Replaces `std::mutex` with an `std::atomic_flag`.

```text
Producer ──► try_lock()
                 │
              locked?
              /     \
            yes      no
             │        │
             ▼        ▼
           work     spin
                      │
                      └──► try again
```

The thread continuously checks the lock while waiting.

This avoids blocking, but can waste CPU time when contention occurs.

---

### 3. Naive Lock-Free SPSC

Removes locks completely and uses atomic producer/consumer indices.

```text
                Ring Buffer

        ┌────┬────┬────┬────┬────┐
        │    │    │    │    │    │
        └────┴────┴────┴────┴────┘
          ▲                   ▲
          │                   │
       Consumer            Producer
        index                index
```

The producer:

1. Checks whether the queue is full.
2. Writes the element.
3. Publishes the new producer index.

The consumer:

1. Checks whether the queue is empty.
2. Reads the element.
3. Publishes the new consumer index.

The queue uses acquire/release memory ordering to safely publish data between the two threads.

---

### 4. Optimized Lock-Free SPSC

The final implementation reduces unnecessary communication between the producer and consumer.

```text
             Producer
                │
                ▼
        ┌───────────────┐
        │ Local index   │
        └───────┬───────┘
                │
          enough space?
           /          \
         yes           no
          │             │
          ▼             ▼
       write          check
        data         consumer
          │
          ▼
     continue
```

The producer and consumer primarily work with their own local indices.

The other thread's index is checked only when necessary.

---

## Key Optimizations

### 1. Local Index Caching

Instead of repeatedly reading the other thread's atomic index, each thread maintains a cached copy.

```text
Producer
   │
   ├── local producer index
   │
   └── cached consumer index
```

The consumer follows the same approach.

This reduces unnecessary cross-core memory traffic.

---

### 2. Direct Index Updates

The queue uses normal atomic `store` operations when publishing an updated index instead of unnecessary read-modify-write operations.

```cpp
tail.store(next, std::memory_order_release);
```

This keeps the update simple and avoids unnecessary atomic operations.

---

### 3. Cache-Line Separation

Producer and consumer indices are placed on separate cache lines.

```text
Cache Line 1
┌──────────────────────────────────────────────┐
│ Producer Index                               │
└──────────────────────────────────────────────┘

Cache Line 2
┌──────────────────────────────────────────────┐
│ Consumer Index                               │
└──────────────────────────────────────────────┘
```

Implemented using:

```cpp
alignas(64)
```

This prevents both threads from repeatedly modifying data located on the same cache line.

---

## Queue Structure

The optimized queue is a fixed-size ring buffer.

```text
             ┌──────────────────────────┐
             │        Ring Buffer       │
             │                          │
             │ [0][1][2][3][4][5][6][7]│
             │  ▲                 ▲     │
             │  │                 │     │
             │ head              tail    │
             │ consumer         producer │
             └──────────────────────────┘
```

When an index reaches the end of the buffer, it wraps around:

```text
index = (index + 1) % capacity
```

For capacities that are powers of two, this can be optimized to:

```cpp
index = (index + 1) & (capacity - 1);
```

---

## Performance Progression

```text
Mutex
  │
  │  8.41 Mops/s
  ▼
Spinlock
  │
  │  2.79 Mops/s
  ▼
Naive Lock-Free
  │
  │  13.26 Mops/s
  ▼
Optimized Lock-Free
  │
  │  86.72 Mops/s
  ▼
```

The final implementation achieves approximately:

**10.3× the throughput of the mutex-based implementation.**

---

## Project Structure

```text
.
├── LockSPSC.cpp
├── SpinLockSPSC.cpp
├── spsc_queue.cpp
├── OptimizedSPSC.cpp
├── main.cpp
└── screenshots/
    └── benchmark.png
```

---

## Build

Compile with C++20 and optimization enabled:

```bash
g++ -O3 -std=c++20 -Wno-psabi -pthread main.cpp -o spsc_benchmark
```

Run:

```bash
./spsc_benchmark
```

---

## Requirements

- C++20 compatible compiler
- POSIX threads support
- Linux/macOS environment recommended for benchmarking
- Optimization enabled with `-O3`
