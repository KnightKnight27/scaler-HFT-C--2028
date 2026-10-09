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

<img width="778" height="133" alt="Screenshot 2026-10-09 at 20 25 22" src="https://github.com/user-attachments/assets/569ba15d-2b59-400f-8cda-b6f0a89c13a2" />


