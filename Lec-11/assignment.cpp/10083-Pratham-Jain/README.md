# Lec-11: SPSC queue with spinlock and std::mutex

- name: Pratham Jain
- email: pratham.24bcs10083@sst.scaler.com
- roll_no:24bcs10083

## Assignment

Implement a single-producer, single-consumer queue using a lock and measure how many 64-byte objects it can push and pop in one second. This submission compares a spinlock and `std::mutex` using the same queue and benchmark code in [spsc_queue.cpp](spsc_queue.cpp).

## Implementation

- `Order` contains an eight-byte sequence number and seven eight-byte payload values. `static_assert(sizeof(Order) == 64)` verifies the object size.
- `SPSCQueue<Lock, Capacity>` uses a fixed `std::array` memory pool. The default capacity is 1,024 objects (64 KiB of object storage), allocated as part of the queue before the threads start. Slots are constructed once and reused; push/pop perform no heap allocation.
- The capacity must be a nonzero power of two. Unsigned read/write counters select a slot using `index & (Capacity - 1)`. The queue is full when `write - read == Capacity` and empty when `write == read`.
- `std::lock_guard` protects both counters and the object copy for every push/pop. The counters do not need to be atomic because all concurrent access is under the same lock.
- The spinlock uses `atomic_flag` with acquire/release ordering. Its inner loop only reads the flag while the lock is held; it busy-waits and does not provide a fairness guarantee.
- `pop(Order&)` copies into the caller's object. It returns `false` for an empty queue; `push` returns `false` for a full queue. The benchmark retries these cases.

## Build and run

A C++20 compiler and thread support are required. From this submission directory:

```bash
c++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue --test
./spsc_queue       # three rounds for each lock, one second per run
./spsc_queue 1     # one round for each lock

# Unoptimized comparison
c++ -std=c++20 -O0 -Wall -Wextra -Wpedantic -Werror -pthread spsc_queue.cpp -o spsc_queue_O0
./spsc_queue_O0 3
```

Each benchmark row reports successful pushes/pops, elapsed seconds, million completed objects/second, MiB/second, full/empty retries, remaining objects, and validation status. Invalid arguments or failed validation produce a nonzero exit status.

## Measurement method

One producer thread creates sequential orders and one consumer thread verifies the sequence and all seven payload values. Both signal readiness and wait for a common start flag. The main thread requests stop after one second and calls `t1.join()` and `t2.join()`.

Elapsed time uses `std::chrono::steady_clock` and includes the short stop/join overhead, so runs are slightly longer than exactly one second. Completed transfers per second are `popped / elapsed_seconds`; each completed transfer represents one successful push and one successful pop. MiB/s is `popped * 64 / elapsed_seconds / 1048576`, counting each transferred object once.

Pending objects are drained and validated **after** timing and excluded from the throughput count. Every run checks `pushed == popped + remaining`, FIFO order, and payload integrity. Counters are local to each worker during the run, with totals published at exit. The lock execution order reverses each round. These measurements include order creation and validation overhead.

## Machine used

- CPU: Apple M5 Pro, arm64
- Memory: 24 GiB
- OS: macOS 27.0, build 26A428
- Compiler: Apple clang 21.0.0 (clang-2100.3.34.2), libc++
- Flags: `-std=c++20 -pthread`, with `-O0` or `-O2` as below; no sanitizers during timing
- Three rounds per lock; no thread affinity or dedicated machine isolation

## Measured results

### `-O2`

| Round | Lock | Pushed | Popped | Elapsed (s) | Million objects/s | MiB/s |
|---:|---|---:|---:|---:|---:|---:|
| 1 | spinlock | 6,211,288 | 6,210,358 | 1.005067 | 6.179 | 377.139 |
| 1 | std::mutex | 13,661,368 | 13,660,345 | 1.000606 | 13.652 | 833.256 |
| 2 | std::mutex | 13,608,854 | 13,608,668 | 1.000934 | 13.596 | 829.832 |
| 2 | spinlock | 4,411,522 | 4,410,499 | 1.005029 | 4.388 | 267.849 |
| 3 | spinlock | 6,798,224 | 6,797,237 | 1.005041 | 6.763 | 412.789 |
| 3 | std::mutex | 14,075,629 | 14,075,032 | 1.005030 | 14.005 | 854.772 |

### `-O0`

| Round | Lock | Pushed | Popped | Elapsed (s) | Million objects/s | MiB/s |
|---:|---|---:|---:|---:|---:|---:|
| 1 | spinlock | 1,957,936 | 1,956,912 | 1.004997 | 1.947 | 118.847 |
| 1 | std::mutex | 2,819,047 | 2,818,024 | 1.005037 | 2.804 | 171.137 |
| 2 | std::mutex | 2,871,580 | 2,870,557 | 1.005031 | 2.856 | 174.328 |
| 2 | spinlock | 2,082,671 | 2,081,648 | 1.005035 | 2.071 | 126.417 |
| 3 | spinlock | 1,843,034 | 1,842,010 | 1.002828 | 1.837 | 112.110 |
| 3 | std::mutex | 2,880,058 | 2,879,034 | 1.002125 | 2.873 | 175.350 |

### Average normalized throughput

| Build | Spinlock (million objects/s) | std::mutex (million objects/s) |
|---|---:|---:|
| `-O2` | 5.777 | 13.751 |
| `-O0` | 1.952 | 2.844 |

All 12 runs passed validation. On this machine, `std::mutex` achieved about 2.38 times the spinlock throughput at `-O2`. The optimized spinlock varied from 4.388 to 6.763 million objects/s, while the mutex ranged from 13.596 to 14.005 million objects/s.

The spinlock burns CPU while waiting and can repeatedly reacquire the lock, preventing the other thread from progressing. For example, its second optimized round had 17,312,136 full retries for 4,410,499 completed transfers. The mutex implementation and scheduler may reduce some of this contention, but these throughput measurements alone do not establish the exact cause. Results depend on scheduling, background load, processor, and standard library; they do not show that mutexes are universally faster. `-O2` also optimizes the object creation and validation work, so its improvement cannot be attributed solely to locking.

## Correctness checks

`--test` checks empty/full behavior, capacity one, repeated ring wraparound, FIFO ordering, all payload words, and 100,000 concurrent transfers through a four-slot queue for **each** lock. The benchmark additionally verifies every timed and leftover object.

The correctness suite passed with AddressSanitizer + UndefinedBehaviorSanitizer and separately with ThreadSanitizer, with no diagnostics. Reproduce these checks separately from timing:

```bash
clang++ -std=c++20 -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer spsc_queue.cpp -o spsc_asan
./spsc_asan --test
clang++ -std=c++20 -O1 -g -pthread -fsanitize=thread spsc_queue.cpp -o spsc_tsan
./spsc_tsan --test
```
