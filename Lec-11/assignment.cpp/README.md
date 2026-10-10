# Lec-11: SPSC queue

email: "saniya.24bcs10246@sst.scaler.com"
roll_no: "24bcs10246"

## Implementation

`spsc_queue.cpp` implements a bounded single-producer, single-consumer queue
using `std::mutex`. Each object contains eight 64-bit words, and a
`static_assert` checks that its size is exactly 64 bytes.

The queue uses a preallocated ring of 4096 objects (256 KiB of payload storage).
This ring acts as a fixed object pool: a slot is reused after the consumer pops
its object. There are no per-object heap allocations during push or pop.
The head, tail, size, and closed flag are protected by the same mutex.
`try_push` returns false when the queue is full or closed; `try_pop` distinguishes
an empty queue from a closed, drained queue. The producer and consumer retry
full/empty operations with `std::this_thread::yield()`.

After the timed interval, the producer closes the queue. The consumer drains
remaining objects and exits. Both threads are joined before results are read.
This uses locks as requested; it is not a lock-free queue.

## Build and run

From the repository root, with a C++17 compiler:

```sh
clang++ -std=c++17 -O3 -pthread -Wall -Wextra -Wpedantic \
  Lec-11/assignment.cpp/spsc_queue.cpp -o /tmp/spsc_queue
/tmp/spsc_queue --test
/tmp/spsc_queue
```

`g++` can be used instead of `clang++` with the same flags.

## Benchmark method

- One producer pushes objects; one consumer pops them.
- Queue allocation and thread creation happen before timing starts.
- A condition-variable start gate waits until both workers are ready, then
  releases them with a common `steady_clock` deadline one second later.
- The program performs a 0.2-second warmup, followed by three independent
  one-second runs, each with a fresh queue.
- Successful pushes and pops are counted separately using timestamps taken
  immediately after the operation. Failed full/empty attempts are excluded.
- Every popped object's eight words are checked against the expected sequence,
  detecting corruption, reordering, missing objects, and duplicates.
- Pops after the deadline are reported separately and excluded from throughput.
  Total pushes and pops must match after draining.

The reported end-to-end rate is `pops_in_1s / 1 second`. An object is counted once
as a completed transfer; push and pop counts are not added together. Clock reads,
payload generation, payload validation, mutex contention, and retry/yield costs
are included in the measurement. A push started just before the deadline can
finish afterward; it is included in the drained total, not the timed push count.

## Measured results

Measured on 10 October 2026 on an Apple Silicon `arm64` machine, macOS 26.5.1,
using Apple Clang 17.0.0 and the optimized build command above. Threads were not
pinned to particular cores; other desktop applications were running.

| Run | Pushes in 1 second | Pops in 1 second | Pops after deadline | Total transferred | Elapsed including drain (s) | Payload MiB/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 5,627,677 | 5,627,677 | 1 | 5,627,678 | 1.000003 | 343.49 |
| 2 | 5,410,149 | 5,410,127 | 23 | 5,410,150 | 1.000003 | 330.21 |
| 3 | 5,488,043 | 5,487,957 | 87 | 5,488,044 | 1.000007 | 334.96 |

**Median: 5,487,957 completed 64-byte transfers per second (334.96 MiB/s).**
Payload bandwidth is `pops_in_1s * 64 / (1024 * 1024)`; this is not a measurement
of total memory traffic. Results depend on hardware, scheduling, and system load.

Raw output from the measured run:

```text
std::mutex SPSC queue; object=64 bytes; capacity=4096; window=1 second
run,pushes_in_1s,pops_in_1s,drained_after_1s,total_transferred,elapsed_with_drain_s,payload_MiB_per_s
1,5627677,5627677,1,5627678,1.000003,343.49
2,5410149,5410127,23,5410150,1.000003,330.21
3,5488043,5487957,87,5488044,1.000007,334.96
Validation passed: every pushed object was popped in FIFO order, with all 64 bytes checked.
```

## Validation

The optimized build compiled without warnings and passed `--test`. The tests
cover zero-capacity rejection, empty/full behavior, wraparound, rejection of
pushes after close, draining after close, and 100,000 concurrent transfers at
each of capacities 1, 3, and 4096. All measured benchmark runs also passed FIFO,
payload, and final push/pop count checks.

The same tests passed with UndefinedBehaviorSanitizer:

```sh
clang++ -std=c++17 -O1 -g -pthread -fsanitize=undefined \
  -fno-omit-frame-pointer Lec-11/assignment.cpp/spsc_queue.cpp \
  -o /tmp/spsc_queue_ubsan
/tmp/spsc_queue_ubsan --test
```

```text
Tests passed: empty, full, wraparound, close/drain, zero capacity, and 100000 transfers each at capacities 1, 3, 4096.
```
