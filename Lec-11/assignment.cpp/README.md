# SPSC Lock-Free FIFO

Implementation and performance optimization of a Single Producer Single Consumer (SPSC) FIFO in C++.

The goal is to measure the throughput of 64-byte objects and compare the performance impact of different low-level optimizations.

## Benchmark Setup

* Object size: 64 bytes
* Queue capacity: 1024
* Iterations: 100,000,000
* Compiler: g++ -O2
* Environment: Ubuntu / WSL

## Results

| Version             | Average Ops/sec |
| ------------------- | --------------: |
| Baseline (seq_cst)  |     ~8.79 M     |
| Acquire/Release     |     ~9.56 M     |
| Relaxed             |     ~9.63 M     |
| Power-of-2 indexing |     ~9.73 M     |
| removed extra copy  |     ~9.95 M     |
    (proxy method)
| Alignment           |     ~10.11 M    |
| Cached cursors      |     ~13.66 M    |
| Final               |             TBD |

## Optimizations

The queue will be optimized incrementally and benchmarked after each major change:

1. Acquire/Release memory ordering
2. Relaxed memory ordering
3. Power-of-2 indexing
4. Cursor optimization
5. False-sharing prevention
6. Cached cursors
7. Final optimization

## Resources and References

The optimization ideas explored in this assignment were inspired by concepts learned in daily classes and the following educational resources:

* Daily Classes: Concepts and optimization techniques discussed during lectures.

* C++ and Performance Engineering: [CppCon YouTube Channel](https://youtu.be/K3P_Lmq6pw0?si=CJ5Z6zzw7nwnZzvX) — talks on modern C++, concurrency, memory ordering, cache behavior, and performance optimization.

These resources helped guide the exploration of memory ordering, cache-line alignment, power-of-two indexing, and cached producer/consumer indices.

