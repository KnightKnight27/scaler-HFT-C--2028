# HFT C++ Assignment - Lec 11

**Name:** Ayush Kumar Patra  
**Roll No:** 24bcs10474

## Architecture & Design Patterns

### Cache Line Alignment & False Sharing
In multi-threaded applications, CPUs cache memory in chunks called **cache lines** (typically 64 bytes on modern x86 architectures). 
**False sharing** occurs when two distinct variables, modified by different threads, happen to reside on the same cache line. 
This causes the CPU cache to repeatedly invalidate and fetch the cache line, severely degrading performance.
To avoid this in our `SPSCQueue`:
- We padded the `Message` struct using `alignas(64)` to ensure each message is neatly aligned and prevents overlap in the buffer array.
- We isolated the `head` and `tail` atomic variables with `alignas(64)` so the producer (updating `tail`) and the consumer (updating `head`) do not contend for the same cache line.

### Memory Orders
We utilized C++ atomic memory orders to guarantee lock-free thread safety without the overhead of strong sequential consistency (`std::memory_order_seq_cst`):
- `std::memory_order_relaxed`: Used when operations do not require synchronization or ordering constraints with other atomic operations (e.g., loading `tail` in `push()`).
- `std::memory_order_acquire`: Used in `pop()` when checking `tail` and loading `head`. It ensures that no memory reads or writes in the current thread can be reordered before this load. It pairs with `release`.
- `std::memory_order_release`: Used in `push()` when updating `tail`. It ensures that all memory writes (such as writing the actual payload to the buffer) are visible to other threads that perform an `acquire` load on `tail`.

## Benchmark Results (1-Second Run)

| Queue Implementation       | Operations/Second (Throughput) |
|----------------------------|--------------------------------|
| **LockedQueue (Spinlock)** | ~34,408,959 ops/sec |
| **SPSCQueue (Lock-Free)**  | ~114,653,654 ops/sec |

## Linux `perf stat` Output

```bash
$ perf stat -r 3 ./benchmark
```

- **Context switches:** 0
- **Branch misses:** N/A (Hardware events restricted in WSL)
- **L1 cache misses:** N/A (Hardware events restricted in WSL)
- **Page faults:** 2192

## Compilation Commands

To compile without optimizations (useful for debugging):
```bash
g++ -O0 -std=c++20 -pthread spsc_queue.cpp -o spsc_queue_O0
g++ -O0 -std=c++20 -pthread stack_lock_free.cpp -o stack_lock_free_O0
g++ -O0 -std=c++20 -pthread smart_ppointers.cpp -o smart_ppointers_O0
```

To compile with maximum optimizations (recommended for benchmarking):
```bash
g++ -O3 -std=c++20 -pthread -march=native spsc_queue.cpp -o spsc_queue_O3
g++ -O3 -std=c++20 -pthread -march=native stack_lock_free.cpp -o stack_lock_free_O3
g++ -O3 -std=c++20 -pthread -march=native smart_ppointers.cpp -o smart_ppointers_O3
```
