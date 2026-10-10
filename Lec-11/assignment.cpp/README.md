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
| Mutex     | 4,738,701 | 5,177,728 | 4,565,391 | 4,827,273 |
| Spinlock  | 3,333,760 | 3,951,047 | 3,830,322 | 3,705,043 |

The mutex had higher average throughput in these runs. A spinlock burns CPU
while waiting; under contention that can waste cycles, while a mutex can yield
the processor and let its thread sleep until the lock is available.

Correctness: `pushes == pops` in all six trials, so no item was lost or duplicated.

## Build and run

Run these commands in the MSYS2 UCRT64 shell from this directory:

```sh
g++ -O2 -std=c++17 main.cpp spsc_queue.cpp -pthread -o bench.exe
./bench.exe
```

The benchmark runs three one-second trials for each lock type and prints the
per-trial counts and average throughput.
