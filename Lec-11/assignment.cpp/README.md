# SPSC queue benchmark (spinlock vs mutex)

email: anika.24bcs10409@sst.scaler.com
roll no: 10409

## what this is

A single producer single consumer (SPSC) queue built on a ring buffer, protected by a lock.
One thread pushes 64 byte objects, one thread pops them, and I count how many objects
get through in 1 second for each lock type.

Goal: compare how much the choice of lock (spinlock / own mutex / std::mutex) affects throughput.

## files

| file | what it does |
|------|--------------|
| `ringbuffer.h` | fixed size circular buffer, not thread safe by itself |
| `spinlock.h` | `spinlock` (atomic, busy wait) and `naiveSpin_lock` (plain bool, broken on purpose) |
| `mutex.h` | `mymutex`, my own lock that yields the cpu instead of spinning |
| `spsc_queue.h` | ring buffer + any lock type (template) |
| `mutex_queue.h` | ring buffer + std::mutex written out by hand |
| `test_ringbuffer.cpp` | unit tests for the ring buffer (empty, full, wraparound, order) |
| `naive_test.cpp` | shows why the naive spinlock fails |
| `lock_test.cpp` | same counter test but 10M iters, prints ok/BROKEN |
| `bench.cpp` | 1 second benchmark for spinlock, mymutex and std::mutex |
| `bench_mutex.cpp` | standalone benchmark for std::mutex only |
| `latency_bench.cpp` | latency version: ns per lock+unlock and push->pop time |
| `spsc_queue.cpp` | single file version, everything inline (queue + locks + bench) |
| `Makefile` | `make` builds test, bench, bench_mutex, naive |
| `TerminalSS.png` | screenshot of a bench run |

## architecture

```
  producer thread (t1)                       consumer thread (t2)
        |                                           |
        |  push(obj)                       pop(obj) |
        v                                           v
   +------------------------------------------------------+
   |                    spsc_queue                        |
   |   lock()  ->  RingBuffer push / pop  ->  unlock()    |
   +------------------------------------------------------+
                           |
                           v
        buf[0] buf[1] buf[2] ... buf[N-1]   (N = 1024)
          ^                    ^
        head%N               tail%N
       (next read)         (next write)
```

The design is split into 3 layers:

1. **storage**: `RingBuffer<T,N>` only knows how to store and return items in FIFO order.
   It has no idea about threads.
2. **locking**: the lock type `L` is a template parameter. Anything with `lock()` and
   `unlock()` works, so the same queue code is used for every lock. This way the only
   difference between benchmark runs is the lock itself.
3. **benchmark**: two threads plus a timer, which count how many objects get through.

### ring buffer

- `head` = how many pops so far, `tail` = how many pushes so far
- both only increase; the array index is `head % N` / `tail % N`
- **empty** when `tail == head`
- **full** when `tail - head == N`

Why I do it this way: with the usual approach (wrapping head/tail back to 0) you can't
tell full from empty when head == tail, so you need an extra count variable or you waste
one slot. Letting them keep counting avoids both. `size_t` is 64 bit so overflow isn't
a real concern.

My first version had two bugs:
- `if(tail>N)` let tail reach N, so it wrote `buf[N]`, which is out of bounds
- there was no wraparound, so after N pushes it stayed "full" forever, even after pops

### the locks

**spinlock**
```
lock:   while(f.exchange(true)) {}
unlock: f = false
```
`exchange` sets f to true and returns the old value in one atomic step. If the old value
was false, we got the lock. If it was true, someone else has it, so we keep trying.
The thread never sleeps. It burns cpu while waiting, but there is no os involvement,
so it's fast when the lock is held for a very short time (like here).

**naiveSpin_lock (broken on purpose)**
```
lock:   while(f) {}   f = true
```
Two problems:
1. checking `f` and setting `f` are two separate steps. Both threads can see `f==false`
   at the same time and both enter the critical section.
2. `f` is a plain bool, so this is a data race (undefined behaviour). With `-O2` the
   compiler can assume f never changes inside the loop and turn it into an infinite loop.

`naive_test.cpp` shows this: two threads each increment a counter 1M times.
spinlock gives exactly 2M; naive gives less (lost updates) or hangs.

**mymutex**

Same as spinlock, but calls `std::this_thread::yield()` when the lock is taken,
so the os can run something else instead of wasting the core.

**std::mutex**

The real one. On linux it is built on a futex: if the lock is free it's taken in user
space with an atomic op (cheap), and if not, the thread can go to sleep in the kernel
and gets woken up on unlock (expensive syscall, but no cpu wasted).

### the object

```cpp
struct obj { long long id; char pad[56]; };   // 64 bytes
```
64 bytes = one cache line on most x86 cpus. `id` is used to check ordering.

### benchmark method

1. create queue (size 1024)
2. start producer: keeps pushing objects with id = 0,1,2,... (retries if full)
3. start consumer: keeps popping, checks `id == popped` (FIFO order), counts
4. main thread sleeps 1 second, then sets `stop = true` (atomic)
5. join both threads
6. drain leftover items so the next run starts with an empty queue
7. print pushed / popped; **popped per second is the result**
8. repeat 5 times per lock and take the average

`stop` is atomic because main writes it while the other threads read it.
`pushed` and `popped` are plain variables because each is written by only one thread
and only read by main after `join()`.

## build & run

```
make
./test          # ring buffer tests
./naive         # naive vs real spinlock
./bench         # spinlock vs mymutex vs std::mutex
./bench_mutex   # std::mutex only
```

## machine

- cpu: Apple M2
- cores / threads: 8 / 8
- ram: 8 GB
- os: macOS 15.6.1
- compiler + version: Apple clang 17.0.0 (`g++ -O2 -pthread`, g++ is clang alias on mac)

## results (64 byte objects popped per second, avg of 5 runs)

| lock       | run1 | run2 | run3 | run4 | run5 | avg |
|------------|------|------|------|------|------|-----|
| spinlock   | 2102021 | 2470109 | 2645454 | 3539059 | 2465637 | ~2.6M |
| mymutex    | 21163531 | 21731950 | 20869422 | 21160180 | 22057566 | ~21.4M |
| std::mutex | 17603150 | 16040296 | 17694395 | 17750797 | 16918662 | ~17.2M |

naive_test output:
```
spinlock  counter = 2000000  (expected 2000000)
naive     counter = 1058012  (expected 2000000)
```

## observations

- which lock was fastest and by how much: mymutex won at ~21.4M/sec, then std::mutex
  ~17.2M, spinlock way behind at ~2.6M. honestly expected spinlock to win since it
  never sleeps. what actually happens: while one thread holds the lock the other keeps
  hammering `exchange` on the same cache line, which slows the holder down. yield gives
  the holder breathing room. std::mutex also does ok but kernel sleep/wake costs more
  than yield in a loop this tight.
- did the numbers vary a lot between runs: mymutex and std::mutex were stable (within
  ~5%), spinlock jumped around a lot (2.1M to 3.5M) - depends who wins the exchange race.
- what happened with the naive lock: counter came out at 1058012 instead of 2000000,
  about half the increments lost. both threads read f==false at the same time and both
  entered. didnt hang this time (-O0) but with -O2 it can loop forever.

## limitations / things I would improve

- the 1 second is approximate (sleep_for can oversleep a bit). Measuring real elapsed
  time with `chrono::steady_clock` and dividing would be more accurate.
- every push/pop takes the lock, even though SPSC doesn't strictly need one. A lock free
  version with atomic head/tail (acquire/release) should be much faster; that's the next step.
- head and tail sit next to each other in memory, so producer and consumer keep
  invalidating each other's cache line (false sharing). Putting them on separate cache
  lines with `alignas(64)` would help.
- results depend on whether the two threads run on the same core or different cores.
  Pinning threads with `taskset` / `pthread_setaffinity_np` would make runs more consistent.