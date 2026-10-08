# SPSC queue assignment

Mohit Kumar · Roll 10222 · mohit.24bcs10222@sst.scaler.com · GitHub: mohit-1710

This implements the [Lec-11 assignment](../spsc_queue.cpp): one producer pushes 64-byte objects, one consumer pops them, and both threads are joined. The same queue is measured with `std::mutex` and a spinlock.

## Build and run

The default commands require `clang++` with C++17 support, `make`, and Python 3 for the test/recording scripts. No external libraries are needed. Run these commands from this directory:

```sh
make
make test
make benchmark
```

`make benchmark` prints CSV for five one-second trials of each lock. To save a new run with its machine details and summary:

```sh
python3 record_benchmark.py results/rerun
```

The benchmark arguments are duration in seconds, measured trials per lock, queue capacity, and lock choice:

```sh
./build/spsc_bench 1 5 1024 mutex
./build/spsc_bench 1 5 1024 spin
./build/spsc_bench --help
```

Defaults are `1 5 1024 both`. The recording script requires `clang++` and forces a fresh build before measuring. GCC users can build, test and benchmark with:

```sh
make clean
make CXX=g++ test
make CXX=g++ benchmark
```

## Results

Measured on 8 October 2026: Apple M5 Pro, 15 logical CPUs, 24 GiB RAM, macOS 26.5.1, arm64, Apple Clang 21.0.0. The release build uses `-std=c++17 -pthread -O3 -DNDEBUG`, with warnings enabled. Queue capacity is 1,024 objects, so the reusable object buffer occupies 65,536 bytes.

| Lock | Median objects/sec | Minimum | Maximum | Median payload MB/sec |
| --- | ---: | ---: | ---: | ---: |
| `std::mutex` | 8,901,400 | 8,864,926 | 8,908,052 | 569.69 |
| Spinlock | 8,332,156 | 8,288,582 | 8,405,278 | 533.26 |

These are five measured one-second windows per variant, after one 0.1-second warm-up. Every sample finished with equal total push/pop counts and zero payload errors. See [raw CSV](results/benchmark.csv), [summary](results/summary.json), and [machine details and source hashes](results/environment.json). The saved run used an existing release build; a forced Clang rebuild reproduced its recorded SHA-256.

One counted object means a successful push followed by a successful pop within the timing window. Push and pop are not added together. Both workers wait at a start gate; the main thread then publishes a shared deadline using `steady_clock`.

The producer checks the deadline before each push attempt. A push already in progress may finish later. The consumer timestamps each successful pop and counts it only if the timestamp is within the window. Objects popped later are reported as `drained_after_window` and excluded from objects/sec. `elapsed_including_drain_seconds` includes final draining and joins.

The consumer checks the FIFO sequence and all eight 64-bit words, including objects drained after the deadline. The clock checks, payload generation/validation, locking, copies and retry loops are part of the measured workload. Payload MB/sec is objects/sec × 64 ÷ 1,000,000. The rate measures this complete producer/consumer workload.

The mutex was faster in this run. Both versions yield when the queue is full or empty; the spinlock also yields after a failed lock attempt. Threads were not pinned, background activity was uncontrolled, and macOS may schedule workers on different core types.

## Queue design

`SPSCQueue<T, Lock>` owns a fixed array allocated at construction. This is the memory pool: popped slots are reused, with no allocation or deallocation in `try_push` or `try_pop`.

The queue stores trivially copyable objects with nonthrowing copy assignment. It rejects zero capacity, supports any positive capacity, and cannot be copied or moved. Destroy it after joining its users.

`head_` is the next slot to pop, `tail_` is the next slot to fill, and `size_` distinguishes full from empty even when the indices are equal. Each operation holds the same lock for its state check, object copy and index update. A full push returns `false` without changing the queue; an empty pop returns `false` without changing the output object. All allocated slots are usable, including when capacity is one.

For the spinlock, `atomic_flag::test_and_set` with acquire ordering grants ownership and `clear` with release ordering publishes the updates when the lock is released. The mutex provides the same exclusion through `lock_guard`.

The queue is lock based and its lock acquisition can wait; the `try_` names describe the full/empty check, not a lock-free operation. A spinlock has no fairness guarantee and can waste CPU under contention.

The producer's separate atomic completion flag uses release/acquire ordering. When the consumer sees an empty queue and then sees completion, it checks the queue once more before exiting. This covers a final push occurring between those two observations.

## Verification

```sh
make test
make asan
make tsan
```

All passed on the Mac described above:

- 200,602 C++ checks covering zero capacity, empty/full behaviour, unchanged failed output, FIFO order, full-slot use, repeated wraparound and mixed pushes/pops.
- 18 concurrent trials transferring 4,500,000 objects: capacities 1, 7 and 1,024; normal scheduling, a slower producer, and a slower consumer; both locks. Each trial verifies the entire object and final empty state.
- 72 CLI/benchmark checks covering invalid arguments, requested lock variants, timed transfers, rate arithmetic and drain accounting.
- AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer on the C++ tests and benchmark CLI checks. Logs: [ASan/UBSan](results/asan-ubsan.log), [TSan](results/tsan.log).

Sanitizer runs are correctness checks; their timings are not used in the results table. These runs cover the listed capacities and schedules. The saved sanitizer logs are from Apple Clang 21.0.0 on the Mac described above.

AI assistance was used to draft the implementation, tests and explanation. The reported measurements and checks were actually run on the recorded machine.
