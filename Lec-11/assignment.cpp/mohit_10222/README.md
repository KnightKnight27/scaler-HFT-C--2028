# SPSC queue assignment

Mohit Kumar · Roll 10222 · mohit.24bcs10222@sst.scaler.com · GitHub: mohit-1710

[Lec-11 assignment](../spsc_queue.cpp): one producer pushes 64-byte objects, one consumer pops them, and both threads are joined. The same queue runs with `std::mutex` and a spinlock.

## Run

Use a C++17 compiler and `make`. The default compiler is `clang++`. From this directory:

```sh
make
make benchmark
```

This runs five one-second trials per lock, after a 0.1-second warm-up, with capacity 1,024. To change the duration, trial count, capacity or lock:

```sh
./build/spsc_bench 1 5 1024 mutex
./build/spsc_bench 1 5 1024 spin
./build/spsc_bench --help
```

The program prints CSV. To save another run, choose a new filename:

```sh
./build/spsc_bench 1 5 1024 both > new-run.csv
```

## Results

Measured on 8 October 2026 using an Apple M5 Pro, 15 logical CPUs, 24 GiB RAM, macOS 26.5.1 and Apple Clang 21.0.0. Release flags: `-std=c++17 -pthread -O3 -DNDEBUG`.

| Lock | Median objects/sec | Minimum | Maximum |
| --- | ---: | ---: | ---: |
| `std::mutex` | 8,901,400 | 8,864,926 | 8,908,052 |
| Spinlock | 8,332,156 | 8,288,582 | 8,405,278 |

[Raw trial counts](results/benchmark.csv). Each object is counted once after a successful pop within the shared `steady_clock` window. The producer checks the deadline before each push attempt; an attempt already in progress may finish later. Later pops are drained and verified but excluded from throughput.

The workload includes copying, clock calls, payload checks and retries. Threads were not pinned and background load was uncontrolled. All recorded trials had equal total push/pop counts and zero payload errors.

## Queue and checks

The queue allocates its circular buffer once and reuses the slots. One lock protects the state check, object copy and index update. `try_push` returns false when full; `try_pop` returns false when empty. The consumer checks FIFO order and all eight 64-bit words. After observing producer completion, it rechecks the queue before exiting so the final push is not missed.

Python 3 is needed only for the CLI checks:

```sh
make test
make asan
make tsan
```

These passed on the recorded Mac: 200,602 C++ checks, 18 concurrent trials transferring 4.5 million objects, and 72 CLI checks. ASan/UBSan and TSan runs use the same checks; their timings are excluded from the results above. Tests cover capacities 1, 7 and 1,024, wraparound, full/empty cases, and slower producer/consumer schedules.

AI assistance was used to draft the implementation, tests and explanation. The reported measurements and checks were actually run on the recorded machine.
