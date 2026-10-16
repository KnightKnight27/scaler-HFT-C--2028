# SPSC Queue Benchmark — Spinlock vs Mutex

## Author
- **Name:** Payel Manna
- **Roll No:** 24BCS10400
- **Email:** payel.24bcs10400@sst.scaler.com

## Objective
Implement a bounded Single Producer, Single Consumer (SPSC) queue in C++ to transfer 10 million objects of 64 bytes each and compare the throughput of a spinlock and `std::mutex` under different compiler optimization levels.

## Implementation
- **Language:** C++17
- **Queue:** Circular buffer, capacity 1024
- **Threads:** One producer and one consumer
- **Object size:** 64 bytes
- **Locks:** `std::atomic_flag` spinlock and `std::mutex`
- **Metrics:** Execution time, objects/second, payload rate, checksum

## Compile and Run

```bash
# Without optimization
g++ -O0 -std=c++17 -pthread spsc_queue.cpp -o bench_O0
./bench_O0

# With optimization
g++ -O3 -DNDEBUG -std=c++17 -pthread spsc_queue.cpp -o bench_O3
./bench_O3
```

## Benchmark Results

Median of five runs per configuration:

| Optimization | Lock | Time (s) | Throughput (objects/s) |
|---|---|---:|---:|
| `-O0` | Spinlock | 8.911 | 1.12M |
| `-O0` | Mutex | 6.449 | 1.55M |
| `-O3` | Spinlock | 8.424 | 1.19M |
| `-O3` | Mutex | 5.184 | 1.93M |

## Observations
- `std::mutex` outperformed the spinlock in all recorded runs.
- Median mutex throughput was approximately **38% higher at `-O0`** and **63% higher at `-O3`**.
- Spinlocks use busy-waiting, which can waste CPU resources under contention.
- All runs produced the expected checksum: `49999995000000`.

## Conclusion
For this implementation and test environment, `std::mutex` achieved higher throughput than the spinlock. Results may vary with hardware, scheduling, and workload. This benchmark measures end-to-end queue throughput, not isolated lock-acquisition cost.