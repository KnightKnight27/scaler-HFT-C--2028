email = "bandari.24bcs10359@sst.scaler.com"
roll_no : 24BCS10359


# SPSC Queue Benchmark

This assignment benchmarks a single-producer, single-consumer queue using:

-> A spinlock implemented with `std::atomic<bool>` and a `while` loop.
-> A fixed memory pool for reusable objects.
-> Message objects that are exactly 64 bytes.
-> One producer and one consumer thread.
-> A one-second measurement window.

## Build and run
-> Used -pthread for thread support during compliation and linking.

From this directory:

```bash
g++ -std=c++17 -O0 -pthread main.cpp -o spsc_debug
./spsc_debug
```

For Linux systems with `perf`:

```bash
perf stat -- ./spsc_debug
```

Ran On macOS, using `time` because `perf` is not available on mac:

```bash
time ./spsc_debug
```

## Measured result

The benchmark was run with zero compiler optimizations (`-O0`) and reported:

RESULTS:

sainiketh@sais-MacBook-Pro SPSC_Queue_Assignment % g++ -std=c++17 -O0 -pthread main.cpp -o spsc_debug
sainiketh@sais-MacBook-Pro SPSC_Queue_Assignment % time ./spsc_debug                                 
Message size: 64 bytes
Measured time: 1.00199 seconds
Objects pushed: 5921126
Objects popped: 5921126
Throughput: 5.90939e+06 objects/sec
Values in order: true
./spsc_debug  2.00s user 0.01s system 148% cpu 1.353 total

