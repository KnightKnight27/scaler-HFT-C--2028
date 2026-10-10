# SPSC Queue Benchmark

email: yuvraj.24bcs10405@sst.scaler.com
roll_no: 24bcs10405

## Machine and compiler

- CPU: 13th Gen Intel(R) Core(TM) i7-13650HX
- Core count: 14
- OS: Microsoft Windows 11 Home Single Language
- Compiler: g++ 16.1.0 (Rev5, Built by MSYS2 project), MinGW-w64 UCRT64
- Flags: `-O2 -std=c++17 -pthread`

## Results

Each trial runs the producer for one second using `std::chrono::steady_clock`.
The table reports successful 64-byte objects per second; each trial also checks
that the number pushed equals the number popped.

| Lock type | Trial 1 (objects/sec) | Trial 2 (objects/sec) | Trial 3 (objects/sec) | Average (objects/sec) |
|-----------|----------------------:|----------------------:|----------------------:|----------------------:|
| Mutex     | 4,501,232 | 4,276,968 | 4,553,637 | 4,443,946 |
| Spinlock  | 4,610,409 | 10,805,859 | 5,933,338 | 7,116,535 |

The spinlock had higher average throughput in this run: it avoids a mutex's
possible sleep and kernel context-switch overhead. It burns CPU while waiting,
though, and its trial-to-trial performance varied substantially.

Correctness: `pushes == pops` in all six trials, so no item was lost or duplicated.

## Build and run

Run these commands in the MSYS2 UCRT64 shell from this directory:

```sh
g++ -O2 -std=c++17 main.cpp spsc_queue.cpp -pthread -o bench.exe
./bench.exe
```

The benchmark runs three one-second trials for each lock type and prints the
per-trial counts and average throughput.
