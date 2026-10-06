# SPSC queue assignment

A single producer and a single consumer transfer 64-byte messages through a fixed ring buffer protected by `std::mutex`.

## Submission details

```yaml
email: "mayank.24bcs10220@sst.scaler.com"
roll_no: "24bcs10220"
```

## Build and run

From the repository root:

```sh
cd Lec-11/assignment.cpp
clang++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -pthread spsc_queue.cpp -o /tmp/scaler-spsc
/tmp/scaler-spsc --test
/tmp/scaler-spsc
```

C++17 and the standard library are the only requirements. No external dependencies.

To collect five separate one-second benchmark runs:

```sh
for run in 1 2 3 4 5; do
    /tmp/scaler-spsc
done
```

## Implementation

- `Message` contains eight initialized 64-bit words; `static_assert` checks that it is exactly 64 bytes.
- The queue has 4,096 reusable slots: 256 KiB of payload storage, allocated as part of the queue before timing. No per-message allocation or separate memory-pool allocator.
- The same mutex protects the complete push and pop operations, including the payload and indices. Full/empty operations return `false` and release the lock before the caller retries.
- Exactly one producer and one consumer run. Both are joined before results are printed.

## Measurement

Both threads wait at a start gate. Once both are ready, main records a `steady_clock` timestamp and releases them. The consumer checks a one-second deadline before each pop attempt, then records actual elapsed time on exit and signals the producer to stop.

Throughput is **successful pops / actual elapsed seconds**: one object means one completed producer-to-consumer transfer, not a push plus a pop counted twice. Failed attempts are excluded. A final pop can finish just after the deadline, so the measured duration, rather than exactly `1.0`, is used as the denominator.

The measured work includes mutex operations, full/empty retries, message generation, payload copies, clock checks, and a checksum of all eight words. Setup, thread creation, joins, and console output are outside the measured interval. There is no warm-up or CPU pinning. Each count is written by only one worker and read by main after joining; the queue itself uses the mutex.

The producer may enqueue a few messages after the consumer finishes, before observing the stop flag. These are included only in the printed producer/remaining counts, never in throughput. Pending messages are discarded when the queue is destroyed. This avoids timing a drain after the deadline.

The printed checksum is checked against the expected sum for the consumed sequence. Full per-message ordering and payload checks run separately under `--test`.

## Benchmark results

| Run | Objects consumed | Elapsed seconds | Objects/second |
| --- | ---: | ---: | ---: |
| 1 | 9,483,410 | 1.000001 | 9,483,404 |
| 2 | 9,905,876 | 1.000002 | 9,905,851 |
| 3 | 9,362,768 | 1.000001 | 9,362,757 |
| 4 | 9,470,196 | 1.000002 | 9,470,180 |
| 5 | 9,820,853 | 1.000004 | 9,820,815 |

**Median: 9,483,404 objects/second** (about 9.48 million). Range: 9,362,757–9,905,851 objects/second. Displayed times are rounded to six decimal places; rates use the unrounded durations.

## Correctness checks

`--test` checks empty/full handling, failed pushes preserving queued data, wraparound, FIFO order, and every word of one million messages transferred across two threads. Checks remain enabled with `-DNDEBUG`.

AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
clang++ -std=c++17 -O1 -g -pthread -fsanitize=address,undefined spsc_queue.cpp -o /tmp/scaler-spsc-asan
/tmp/scaler-spsc-asan --test
```

ThreadSanitizer:

```sh
clang++ -std=c++17 -O1 -g -pthread -fsanitize=thread spsc_queue.cpp -o /tmp/scaler-spsc-tsan
/tmp/scaler-spsc-tsan --test
/tmp/scaler-spsc-tsan
```

The optimized correctness run, AddressSanitizer/UndefinedBehaviorSanitizer checks, and ThreadSanitizer correctness and timed runs passed. Sanitized timings are not used in the results table.
