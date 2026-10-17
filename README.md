# Lock-based SPSC queue

email: "guna.24bcs10070@sst.scaler.com"
roll_no: "24BCS10070"

This implements the [Lec-11 assignment](https://github.com/KnightKnight27/scaler-HFT-C--2028/blob/main/Lec-11/assignment.cpp/spsc_queue.cpp): transfer 64-byte objects between a producer and a consumer using locks, join both threads, and measure transfers per second.

## Design

The queue is a bounded circular buffer with a reusable slot pool allocated at construction. Each slot holds a complete 64-byte object; push and pop do not allocate. Default capacity is 1,024 objects (65,536 payload bytes), and other positive capacities work too.

Choose `std::mutex` or an `atomic_flag` spinlock. One lock protects the payload, positions, and occupied count. A full push or empty pop returns `false`; the caller yields and retries. The spinlock uses acquire/release ordering and periodically yields while waiting. Both versions can wait for a lock; neither is lock-free or wait-free.

## Build and test

Requires a C++17 compiler, Make, POSIX threads, and `timeout` for tests.

```bash
make
make test
make sanitize  # AddressSanitizer + UndefinedBehaviorSanitizer
make tsan      # ThreadSanitizer, if supported by the OS/runtime
make benchmark # Five one-second runs for each lock
```

To compile the benchmark directly:

```bash
g++ -O3 -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue mutex 1 1024 5
```

Arguments are lock (`mutex` or `spin`), seconds, queue capacity, and run count. Tests cover empty/full behavior, invalid capacity, FIFO, unchanged output after a failed pop, wraparound, and payload integrity. Concurrent tests send 200,000 objects per lock at capacities 1, 3, and 1,024: 1,200,000 transfers in total. The test command has a 60-second timeout.

GCC and Clang builds passed with warnings treated as errors. ASan, UBSan, LeakSanitizer, and TSan passed, along with 12 short benchmark/drain runs and 11 invalid-argument checks. LeakSanitizer was run outside the tracing sandbox because it cannot work under ptrace.

## Measurement

A transfer counts once after a successful push, pop, and validation of all eight 64-bit words in sequence order. A start gate excludes queue allocation and thread construction. The controller sleeps for about one second, samples the consumer's atomic completed counter, records elapsed `steady_clock` time, and stops the producer. The consumer then drains the queue, and both threads join. Total produced and consumed must match, with no payload errors.

Throughput is the sampled count divided by actual elapsed seconds. Final drain transfers are excluded from that count; CSV output also gives totals including the drain. Scheduling can extend the interval, and the counter snapshot has a small sampling delay. This is not a hard real-time deadline.

The measurement includes payload construction, copying, validation, locking, retries, yields, and the consumer's atomic counter store. There is no allocation or output in the worker loops. Each lock gets five runs without a separate warm-up.

## Results

Measured on **October 17, 2026**:

- AMD Ryzen 7 6800H, 8 cores / 16 logical CPUs.
- Linux 7.2.10-arch1-1; GCC 16.2.1; `-O3 -std=c++17 -pthread`.
- Capacity 1,024; five approximately one-second runs per lock.
- Controller and workers shared the CPU mask `6,8`, two physical cores here. Workers were not individually pinned, and other applications were not isolated.

| Lock | Median transfers/s | Minimum | Maximum | Payload MiB/s at median |
|---|---:|---:|---:|---:|
| `std::mutex` | 14,055,713 | 13,720,042 | 28,813,526 | 857.89 |
| Spinlock | 8,621,643 | 8,607,104 | 8,753,017 | 526.22 |

Every run had matching producer/consumer totals and zero validation errors. The first mutex sample was much faster than the others; all samples are retained. Mutex had the higher median here, but scheduling, contention, frequency changes, and validation overhead affect the result. Payload MiB/s counts 64 useful bytes per transfer, not total memory traffic.

Raw data: [mutex](results/mutex.csv), [spinlock](results/spin.csv), and [machine specifications](results/machine.txt). Reproduce the measurements with:

```bash
# Check lscpu -e=CPU,CORE and choose suitable CPUs on your machine.
taskset -c 6,8 ./build/spsc_queue mutex 1 1024 5
taskset -c 6,8 ./build/spsc_queue spin 1 1024 5
```

`spsc_queue.hpp` holds the queue and payload, `spsc_queue.cpp` runs the benchmark, and `spsc_queue_test.cpp` contains the tests. The Makefile provides build and test targets; results live in `results/`.

## Submission

Branch: `submission/assignment`, based on upstream `main`. Push it to your GitHub fork and open a PR to `KnightKnight27/scaler-HFT-C--2028:main` from an authenticated environment. The README contains the required email and roll number. Project sources and the Makefile are at the branch root.
