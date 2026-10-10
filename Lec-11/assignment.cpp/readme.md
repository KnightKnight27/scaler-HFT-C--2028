email: "rushi.24bcs10144@sst.scaler.com"

roll_no: "10144"

# SPSC Queue with locks (Lec 11 assignment)

One producer thread (`t1`) pushes 64 byte objects into a bounded queue, one consumer
thread (`t2`) pops them. `main` sleeps for 1 second, sets `stop`, then `t1.join()` and
`t2.join()`. The number of objects the consumer popped in that second is the throughput.

## Files

- `spsc_queue.hpp` - the queue + two spinlocks
- `spsc_queue.cpp` - the 1 second benchmark (std::mutex vs spinlock vs TTAS spinlock)
- `test.cpp` - assert based tests (empty/full, wrap around, batch ops, 2 thread ordering)

## Design

- **Object**: `struct alignas(64) Msg { uint64_t seq; char payload[56]; }` - exactly 64 bytes
  (one cache line), checked with `static_assert`.
- **Memory pool**: the ring buffer allocates all its slots once in the constructor.
  `push` / `pop` only copy into / out of a preallocated slot, so there is no `new`/`delete`
  on the hot path. Capacity is rounded up to a power of 2 so the index is `i & mask`.
- **Locks**: the queue is templated on the lock type:
  - `std::mutex`
  - `SpinLock` - `atomic_flag::test_and_set` in a while loop
  - `TTASSpinLock` - test-and-test-and-set: spin on a plain load (with `pause`) and only try the
    atomic exchange when the lock looks free
- **Batch mode**: `push_n` / `pop_n` take the lock once and move up to 32 objects.
- **Correctness**: every object carries a sequence number and the consumer checks they come
  out in exactly the order they went in (`order ok` in the output).

## How to run

```
g++ -std=c++17 -pthread test.cpp -o test
./test

g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## Per second specs

Machine: Intel Core i7-14650HX laptop, Windows 11, g++ 15.2 (MSYS2 ucrt64), `-O2`. Queue = 1024 slots. Each number is the average of 3 one-second runs.

### One lock per object (`push` / `pop`)

| Run | std::mutex (obj/s) | spinlock (obj/s) | TTAS spinlock (obj/s) |
|-----|--------------------|------------------|-----------------------|
| 1   | 5,112,146          | 6,307,890        | 6,713,621             |
| 2   | 4,724,335          | 6,221,228        | 8,646,018             |
| 3   | 5,469,732          | 5,811,031        | 8,619,676             |

### One lock per 32 objects (`push_n` / `pop_n`)

| Run | std::mutex (obj/s) | spinlock (obj/s) | TTAS spinlock (obj/s) |
|-----|--------------------|------------------|-----------------------|
| 1   | 27,058,346         | 38,308,928       | 136,661,504           |
| 2   | 26,917,322         | 45,932,586       | 136,894,069           |
| 3   | 26,180,597         | 43,235,402       | 154,913,152           |

### Summary

| Lock          | 1 obj per lock          | 32 obj per lock             |
|---------------|-------------------------|-----------------------------|
| std::mutex    | **~5.1 M obj/s** (~310 MB/s) | **~27 M obj/s** (~1.6 GB/s)  |
| spinlock      | **~6.1 M obj/s** (~370 MB/s) | **~42 M obj/s** (~2.6 GB/s)  |
| TTAS spinlock | **~8.0 M obj/s** (~490 MB/s) | **~143 M obj/s** (~8.7 GB/s) |

Sample output:

```
sizeof(Msg) = 64 bytes, queue slots = 1024, 1 second per run, 3 runs each

std::mutex     batch=1    avg      5112146 obj/s  best      5541538 obj/s  (  312.0 MB/s)  order ok
spinlock       batch=1    avg      6307890 obj/s  best      7677266 obj/s  (  385.0 MB/s)  order ok
ttas spinlock  batch=1    avg      6713621 obj/s  best      7376578 obj/s  (  409.8 MB/s)  order ok

std::mutex     batch=32   avg     27058346 obj/s  best     28448416 obj/s  ( 1651.5 MB/s)  order ok
spinlock       batch=32   avg     38308928 obj/s  best     45062560 obj/s  ( 2338.2 MB/s)  order ok
ttas spinlock  batch=32   avg    136661504 obj/s  best    140944736 obj/s  ( 8341.2 MB/s)  order ok
```

## Observations

- With one lock per object, every push and pop is a lock + unlock, and the cache line holding
  the lock and the head/tail indices bounces between the two cores. That caps everything at
  a few million objects per second no matter which lock is used.
- On Windows `std::mutex` is the slowest - under contention it can park the thread in the
  kernel, and waking it back up costs far more than the tiny critical section.
- The plain spinlock is a bit better, but both threads keep doing `test_and_set` (a write), so
  the lock's cache line is always being stolen in exclusive mode.
- TTAS is the best: waiting threads only *read* the lock (shared cache line) and `pause`, so the
  owner can finish and unlock without fighting for the line.
- Batching is the big win: taking the lock once per 32 objects amortizes the lock cost and the
  cache line ping-pong, giving 5x (mutex) up to ~17x (TTAS) more objects per second.
