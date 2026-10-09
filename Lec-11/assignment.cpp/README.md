# SPSC Queue with Locks — 64-byte objects/sec

- **email:** anuska.24bcs10021@sst.scaler.com
- **roll_no:** 10021

Single-producer / single-consumer ring buffer (preallocated pool of 1024 × 64-byte slots, no allocation in the hot path), guarded by either:
- a **spinlock** (`std::atomic_flag` + `while` loop), or
- a **`std::mutex`**.

Producer thread (`t1`) pushes, consumer thread (`t2`) pops, for 1 second; then `t1.join()`, `t2.join()` and the counts are printed.

## Build & run
```bash
clang++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc && ./spsc
```

## Results (Apple M1, macOS, clang -O2, 3 runs each)

| Lock        | Popped objects / sec | Throughput    |
|-------------|----------------------|---------------|
| spinlock    | ~0.9 – 1.7 M         | ~55 – 103 MB/s |
| std::mutex  | ~4.4 – 6.3 M         | ~270 – 386 MB/s |

Raw output: see [results.txt](results.txt).

## Notes
- `std::mutex` beat the naive spinlock on M1: the spinlock has no backoff/`yield`, so the two threads hammer the same cache line and the waiter keeps stealing it from the lock holder.
- Both are far slower than a lock-free SPSC (atomic head/tail, no lock) — every push/pop here pays for a lock round-trip and cache-line ping-pong.
- Numbers vary run to run with OS scheduling.
