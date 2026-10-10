# Locked SPSC queue with a reusable memory pool

This C++17 assignment transfers 64-byte packets from one producer to one consumer. It compares `std::mutex` with a custom spinlock over the same queue, measures successful operations in a one-second window, and joins both threads before reporting results.

## Queue design

`BoundedSpscQueue<Capacity, Mutex>` owns a heap-allocated pool of slots. Each slot contains one `Packet64` and an index linking it to the next slot. Two lists share this pool:

- The free list contains slots available for reuse.
- The queued list contains live packets in FIFO order.

A push removes a free slot, copies the packet, and appends the slot to the queued list. A pop removes the oldest queued slot, copies its packet out, and returns the slot to the free list. Both operations take the same lock through `std::lock_guard`. Every slot belongs to exactly one list between operations, so capacity cannot be exceeded and slots cannot be reused before their packets are popped.

The pool is allocated once during construction. Push and pop are O(1) and perform no dynamic allocation. On the measured x64 build, each slot takes 72 bytes: 64 payload bytes plus an 8-byte link. The 1,024-slot pool therefore takes 73,728 bytes, excluding the queue's indices, lock, and allocator overhead. The payload is exactly 64 bytes, enforced by `static_assert`; this does not imply 64-byte alignment.

`try_push` returns `false` when full; `try_pop` returns `false` when empty and leaves the output unchanged. Both calls can wait for their lock. The intended contract is one producer and one consumer, with both threads finished before queue destruction. Copying the queue is disabled.

`SpinMutex` polls an atomic flag with relaxed reads, acquires ownership with an acquire exchange, and unlocks with a release store. It yields after 64 busy probes. This reduces repeated writes to a contended flag but provides no fairness guarantee. The benchmark also yields when the queue is full or empty.

## Files

| File | Purpose |
| --- | --- |
| `spsc_queue.hpp` | 64-byte packet, spinlock, and bounded pool-backed queue |
| `main.cpp` | Synchronized one-second trials, payload validation, and CSV output |
| `tests.cpp` | Boundary, reuse, FIFO, and concurrent delivery checks |
| `CMakeLists.txt` | Builds both executables and registers two CTest checks |
| `benchmark_results.csv` | Unedited output from the five measured trials per lock |

The earlier comment-only `spsc_queue.cpp` is replaced by the header implementation.

## Build and run

Use a C++17 compiler with working `std::thread` and `std::mutex` support. The previously installed MinGW.org GCC 6.3.0 uses the older Win32 thread configuration and fails to compile these types. The recorded run uses portable [w64devkit 2.10.0](https://github.com/skeeto/w64devkit/releases/tag/v2.10.0), GCC 16.2.0, target `x86_64-w64-mingw32`, with the POSIX thread model. Put that toolchain's `bin` directory first on `PATH` in the current terminal before building.

From `Lec-11/assignment.cpp`, these PowerShell commands reproduce the measured optimized build:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic -Werror main.cpp -o build/spsc_bench.exe
g++ -std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic -Werror tests.cpp -o build/spsc_tests.exe
./build/spsc_tests.exe
./build/spsc_bench.exe 5
```

Without an argument the benchmark runs five trials per lock; accepted counts are 1 through 100. On Linux, use the same compiler flags and executable names without `.exe`.

CMake is also supported. On Windows with this toolchain:

```powershell
cmake -S . -B build/cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake -j 2
ctest --test-dir build/cmake --output-on-failure
```

The generator uses the compiler on `PATH`; alternatively set `CMAKE_CXX_COMPILER` and `CMAKE_MAKE_PROGRAM` explicitly. Other platforms can use their usual CMake generator. The table below comes from the direct `-O2` build; CMake's default Release optimization flags may differ.

## Device specifications

The requested device information is recorded below. CPU, physical/logical core count, HP model, usable memory, and Windows version were also checked locally.

| Specification | Value |
| --- | --- |
| Device name | HP |
| Model | HP Laptop 15s-fq2xxx |
| Processor | 11th Gen Intel(R) Core(TM) i3-1115G4 @ 3.00GHz (2.90 GHz) |
| Physical cores | 2 |
| Logical processors | 4 |
| Installed RAM | 8.00 GB (7.65 GB usable) |
| Graphics card | Intel(R) UHD Graphics (128 MB) |
| Storage | 345 GB of 477 GB used |
| Device ID | 0EB8333A-A334-4188-B899-1114ED1DE3BA |
| Product ID | 00356-24642-34156-AAOEM |
| System type | 64-bit operating system, x64-based processor |
| Pen and touch | Pen support |
| Operating system | Windows 11 Home Single Language, version 10.0.26200 |

## Measurement method

Results were collected on 10 October 2026 on the HP machine above using the direct GCC `-O2` build. Queue capacity is 1,024 packets. Each lock has five trials; trial order alternates between mutex-first and spinlock-first.

1. Allocate a fresh empty queue and create the producer and consumer.
2. Wait until both threads reach a condition-variable start gate, then publish one shared `steady_clock` deadline exactly one second ahead.
3. Generate deterministic data in all eight 64-bit words. The producer advances the sequence only after a successful push. The consumer checks every word against the next expected sequence, detecting corruption, reordering, loss, or duplication.
4. Count successful push/pop calls only when their completion timestamp precedes the deadline. Failed full/empty attempts are recorded separately. An operation already waiting for its lock can finish after the deadline; it is excluded from the timed count.
5. After the window, let the consumer drain the queue until the producer finishes and the queue is empty. These extra pops appear in `post_window_pops` and do not increase reported throughput.
6. Join both threads, check total pushed equals total popped, and emit `PASS` or return a failure status.

Thread creation and pool allocation are outside the timed window. Clock reads, lock contention, packet generation, payload checks, and retry/yield costs are included. There is no warm-up or CPU affinity, and background load and power settings were not controlled. This measures the complete validated workload, rather than isolated queue-call latency.

## Measured performance

All entries are successful 64-byte objects per second, because each measurement window is exactly one second. Push and pop counts describe the two ends of the same transfers and must not be added together as transferred-object throughput.

| Trial | Mutex pushes/sec | Mutex pops/sec | Spinlock pushes/sec | Spinlock pops/sec |
| --- | ---: | ---: | ---: | ---: |
| 1 | 4,755,152 | 4,754,130 | 6,017,122 | 6,017,033 |
| 2 | 3,907,037 | 3,907,014 | 4,468,192 | 4,467,512 |
| 3 | 3,486,955 | 3,486,846 | 4,778,390 | 4,778,346 |
| 4 | 4,246,226 | 4,246,123 | 4,735,806 | 4,735,443 |
| 5 | 3,836,065 | 3,835,087 | 4,585,522 | 4,585,522 |

| Summary | std::mutex | SpinMutex |
| --- | ---: | ---: |
| Median pushes/sec | 3,907,037 | 4,735,806 |
| Median completed transfers/sec (pops) | 3,907,014 | 4,735,443 |
| Mean completed transfers/sec | 4,045,840 | 4,916,771.2 |
| Minimum completed transfers/sec | 3,486,846 | 4,467,512 |
| Maximum completed transfers/sec | 4,754,130 | 6,017,033 |
| Median payload throughput (MiB/sec) | 238.47 | 289.03 |

Payload throughput is `pops_per_second * 64 / 1,048,576`; it excludes slot metadata and is not a measurement of physical memory bandwidth. The spinlock's median completed-transfer rate was about 21.2% higher in these samples. Results vary with scheduling and load; these five samples do not establish a universal advantage.

All ten trials reported `PASS`: FIFO order and all payload words matched, and total pushes equaled total pops after draining. The small timed push/pop differences reflect packets still in flight at the deadline. See [benchmark_results.csv](benchmark_results.csv) for total counts, post-window pops, and full/empty retries.

## Correctness checks

Both locks passed tests for initially empty queues, full queues, unchanged output on empty pop, FIFO ordering, and slot reuse while packets remain queued. The boundary tests repeat 100 cycles at capacities 1 and 3. Concurrent tests transfer 200,000 patterned packets at capacity 1 and another 200,000 at capacity 7 for each lock: 800,000 verified packets overall.

The direct build passed with warnings treated as errors. The CMake Release build passed both CTest checks (`queue_correctness` and a one-trial `benchmark_validation` smoke test). Invalid benchmark arguments, including zero, negative, nonnumeric, excessive, and overflowing run counts, returned failure as expected.
