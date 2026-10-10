# SPSC Queue with Spinlock and std::mutex

Name: Harshita Hirawat  
Roll no: 24BCS10044  
Email: harshita.24bcs10044@sst.scaler.com

Assignment: write an SPSC (single producer, single consumer) queue with a spinlock (while loop) or std::mutex, and check how many 64 byte objects we can push and pop in 1 second.

I did both, in 2 files:

- `spsc_spinlock.cpp` - queue with my own spinlock
- `spsc_mutex.cpp` - same queue with `std::mutex`

Both files are same except the lock part, so it's easy to compare them.

## What I made

- **Object**: `struct Order` with a `long long id` and `char data[56]`, so it is exactly 64 bytes (one cache line). There is a `static_assert` to check this.
- **Ring buffer**: 1024 slots. The size is a power of 2, so I use `idx & (size - 1)` instead of `%`, because modulo is slow.
- **Memory pool**: the whole buffer is allocated only once in the constructor with `::operator new`. Push and pop never allocate anything.
- **Indexes**: `mPushIdx` and `mPopIdx` only keep going up and are never reset. `size()` is `push - pop` and `empty()` is `push == pop`, same as the SPSC we wrote in class. Since the lock protects them, they are plain `size_t`, not atomic.
- **pop(T& val)**: pop writes directly into `val`, so no extra copy when returning. After that it calls `~T()` on the slot, like the class SPSC code.
- **[[unlikely]]** on the full and empty checks, because most of the time the queue is neither full nor empty (pipeline stall topic).
- Copy and move are deleted, a queue shared by two threads should not get copied.

Both `push` and `pop` take the lock, change the index, then unlock.

### SpinLock (spsc_spinlock.cpp)

- It is a `std::atomic<bool> flag`.
- `lock()` loops on `compare_exchange_weak(expected, true)` (sir's hint in class). It means "if flag is false, make it true". Check and set happen in one step, so both threads can't get the lock at the same time.
- If it fails, it changes `expected` to the current value (`true`), so I set it back to `false` before trying again.
- `weak` can fail sometimes even when the flag is free, but we are already in a loop so it doesn't matter. That's why weak and not strong.
- It never sleeps, it just keeps spinning in the while loop.
- `lock()` uses `memory_order_acquire` and `unlock()` uses `memory_order_release` (from the memory order class), so the push/pop work inside doesn't move outside the lock.

### std::mutex (spsc_mutex.cpp)

- Just `std::mutex mLock` instead of `SpinLock mLock`. Same `lock()` and `unlock()` calls.
- If the mutex is already taken, the thread can go to the kernel and sleep till it's free, instead of spinning.

## How I tested

- `t1` is the producer. It keeps pushing `Order`s with ids 0, 1, 2 and so on.
- `t2` is the consumer. It keeps popping, and checks that the ids come out in the same order.
- `main` sleeps for 1 second, sets a `stop` flag, then calls `t1.join()` and `t2.join()`.
- The counts printed at the end are how many objects got pushed and popped in that 1 second.
- `pushed` is changed only by `t1` and `popped` only by `t2`. I put `alignas(64)` on both so they are on different cache lines (no false sharing), otherwise the counters also slow down the test.

Build and run (in WSL):

```
g++ -O0 -std=c++17 -pthread spsc_spinlock.cpp -o spin
g++ -O0 -std=c++17 -pthread spsc_mutex.cpp -o mtx
./spin
./mtx
```

I used `-O0` (no optimisation) like in class.

## My system

- CPU: Intel Core i7-1255U (12th gen, 10 cores, 12 threads)
- OS: Ubuntu 26.04 on WSL2 (Windows 11)
- Compiler: g++ 15.2.0, `-O0 -std=c++17`

## Results (64 byte objects in 1 second)

I ran both 6 times, one after the other (spin, mutex, spin, mutex...).

| Run | Spinlock popped in 1 sec | std::mutex popped in 1 sec |
|-----|--------------------------|----------------------------|
| 1   | 5,455,694                | 3,798,211                  |
| 2   | 4,438,644                | 4,048,786                  |
| 3   | 4,595,389                | 3,873,124                  |
| 4   | 5,861,718                | 2,799,809                  |
| 5   | 3,002,028                | 4,390,216                  |
| 6   | 4,506,413                | 3,297,459                  |
| **Avg** | **~4.6 million**     | **~3.7 million**           |

Pushes are almost the same as pops. The small difference is just the items still left in the queue when we stopped. The order check passed in every run.

So in 1 second:
- Spinlock: about **4.6 million** 64 byte objects (about **300 MB/s**)
- std::mutex: about **3.7 million** 64 byte objects (about **240 MB/s**)

## perf stat

```
perf stat -e task-clock,context-switches,page-faults,cycles,instructions,branch-misses ./spin
perf stat -e task-clock,context-switches,page-faults,cycles,instructions,branch-misses ./mtx
```

I ran each 3 times (perf version 7.0.14 on WSL2):

| Metric | Spin 1 | Spin 2 | Spin 3 | Mutex 1 | Mutex 2 | Mutex 3 |
|--------|--------|--------|--------|---------|---------|---------|
| popped in 1 sec | 5,815,026 | 5,548,290 | 6,438,301 | 3,457,556 | 4,459,151 | 4,398,323 |
| task-clock | 2002 ms | 2002 ms | 2002 ms | 1905 ms | 1933 ms | 1949 ms |
| user time | 2.00 s | 1.99 s | 2.00 s | 1.24 s | 1.15 s | 1.22 s |
| sys time | 0.00 s | 0.00 s | 0.00 s | 0.67 s | 0.79 s | 0.74 s |
| context-switches | 0 | 0 | 0 | 0 | 0 | 0 |
| page-faults | 163 | 166 | 163 | 161 | 164 | 163 |

cycles, instructions and branch-misses showed `<not supported>` for all runs.

What these mean:

- **task-clock**: around 2000 ms in 1 sec for spinlock, so 2 CPUs are busy the whole time. Both `t1` and `t2` run at 100%, even when they are just waiting for the lock, because spinlock never sleeps. Mutex is a little less (~1930 ms), because sometimes a thread sleeps in the kernel.
- **sys time**: this was the biggest difference. Spinlock has 0 sys time, everything is in user mode. Mutex spends around 0.7 sec in the kernel. That's when the mutex is taken and the thread goes to the kernel to wait. Going to kernel is slow, that's why mutex got less objects.
- **page-faults ~163**: very less, and same for both. Most of it is from loading the program and making the threads. The queue is only 1024 x 64 bytes = 64 KB (16 pages) and it's allocated once at the start (memory pool), so push/pop don't make new page faults.
- **context-switches 0**: perf added `:u` to every event, which means only user mode is counted (because `perf_event_paranoid` is 2 in WSL). Context switches happen in the kernel, so they are not counted here. So 0 doesn't really mean zero. Main thread sleeps for 1 sec so there is at least that, and the mutex sys time shows its threads are going into the kernel.
- **cycles, instructions, branch-misses**: WSL2 doesn't give the CPU hardware counters (`perf list hw` shows nothing), so perf says `<not supported>`. For these we need real Linux, not WSL.

## What I noticed

- Spinlock was faster, around 1.25x more objects than mutex. The lock is held for a very short time (just one copy and index++), so spinning a bit is cheaper than going to the kernel and coming back.
- From perf, mutex spends a lot of time in the kernel (sys time) and spinlock spends none. But spinlock burns full CPU on both threads even while waiting.
- In spinlock both threads keep doing `compare_exchange_weak` on the same `flag`. So the cache line with the flag keeps going back and forth between the two cores (MESI), and this slows down even the thread holding the lock.
- When one thread has the lock, the other can't do anything. So producer and consumer basically take turns instead of running together. This is same for both locks.
- The lock and both indexes are right next to each other in the queue object, so most probably they are on the same cache line. So every push and pop touches a line the other thread is also using.
- To make it faster, next step will be removing the lock and making the indexes atomic with `alignas(64)`, like the lock-free SPSC we did in class. Then producer and consumer don't have to wait for each other.
- The numbers change a lot every run (spinlock went from 3M to 6.4M). Depends on which cores the threads go on (this CPU has P cores and E cores) and what else is running on the laptop. Spinlock was faster in most runs but not all (in run 5 mutex got more).
