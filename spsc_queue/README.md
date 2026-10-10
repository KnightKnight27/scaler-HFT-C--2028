# Name - Ardhi Jasmine
# Roll No. - 24BCS10557

# SPSC Queue Benchmark: std::mutex vs Spinlock

Two lock-based single-producer / single-consumer (SPSC) queues, each with a
1-second benchmark that measures how many 64-byte objects can be pushed and
popped.

## Files

| File                | Locking strategy                                  |
|---------------------|---------------------------------------------------|
| `spsc_mutex.cpp`    | `std::mutex` (OS-assisted, thread sleeps on wait) |
| `spsc_spinlock.cpp` | Test-and-test-and-set spinlock (busy-wait loop)   |

## Design

- Bounded ring buffer, capacity 1024 (a power of two, so wrap-around uses a bit mask).
- `Object` is `alignas(64)` and exactly 64 bytes (8-byte sequence number + 56-byte payload),
  enforced by a `static_assert`.
- `try_push` / `try_pop` are non-blocking and return `false` when the queue is full / empty.
- One producer thread pushes objects with increasing sequence numbers.
- One consumer thread pops them and checks the sequence is strictly in order (FIFO check).
- Both threads start together on a start flag, run for 1 second, then stop on a stop flag.
- Locks are taken with RAII (`std::lock_guard`), so they are always released.

## Requirements

- g++ or clang++ with C++17 support
- Linux or macOS (pthreads)

## Build

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pthread spsc_mutex.cpp    -o spsc_mutex
g++ -std=c++17 -O2 -Wall -Wextra -pthread spsc_spinlock.cpp -o spsc_spinlock
```

## Run

```bash
./spsc_mutex
./spsc_spinlock
```

## Sample output (numbers will vary by machine)

```
Implementation : spinlock
Object size    : 64 bytes
Elapsed        : 1.00012 s
Pushed         : 12345678
Popped         : 12345100
Throughput     : 12343000 objects/sec
FIFO order     : OK
```

The mutex version prints the same fields, plus the queue capacity and a
MiB/s figure.

### Reading the results

- **Popped** (and **Throughput**) is the main result: objects successfully
  transferred per second.
- `Pushed - Popped` is at most the queue capacity, because some items are
  still in the queue when the timer stops.
- The program exits with code 1 if FIFO order is violated.

## Tips for reliable numbers

- Always build with `-O2` or `-O3`. Debug builds are not meaningful.
- Run each binary several times and compare the median.
- Pin the threads to separate physical cores. On Linux:
  `taskset -c 2,4 ./spsc_spinlock`. The spinlock in particular suffers if the
  OS schedules both threads on one core.
- Close other heavy programs, and avoid running on a laptop in power-saving mode.
- Try changing the capacity constant (it must be a power of two) to see how
  queue size affects throughput.

## Notes

- The spinlock burns CPU while waiting. It can beat `std::mutex` when each
  thread has its own core, but can be much worse if cores are oversubscribed.
- The spinlock uses a plain busy-wait with no CPU pause hint
  (`_mm_pause` / `yield`) to keep the code short. Adding one can improve
  results on some CPUs.
- Both locks are held while the 64-byte object is copied, so that copy time
  is part of what is measured.
- Since this is SPSC, a lock-free ring buffer with atomic head/tail indices
  would normally be faster than either version. These are intentionally
  lock-based to give a baseline for comparison.