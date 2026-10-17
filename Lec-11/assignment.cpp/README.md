# Lec-11 Assignment: SPSC Queue (64-byte objects)

email: "shah.24bcs10447@sst.scaler.com"
roll_no: "10447"

## What it does
Producer thread (`t1`) pushes 64-byte objects, consumer thread (`t2`) pops them.
Runs for 1 second, then prints how many objects made it through. Every run also
checks FIFO order and that pushed == popped.

Three versions of the same fixed-size ring buffer:
1. `std::mutex`
2. spinlock (`atomic_flag` in a while loop)
3. lock-free (atomics only), added to see how big the gap to the locked ones is

Memory pool: the ring buffer (1024 slots x 64 B) is allocated once at startup and
reused, so there is no `new`/`malloc` on the push/pop path.

## Build and run
```
g++ -std=c++17 -O3 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## Test machine
- CPU: Intel Core i3-4005U @ 1.7 GHz, 2 physical cores / 4 threads (hyperthreading), no turbo
- RAM: 4 GB, OS: Arch Linux (kernel 7.1.5-arch1-2), compiler: GCC 16.1.1
- `lscpu -e`: CPUs 0,1 share physical core 0 (same L1/L2), CPUs 2,3 share core 1.
  Everything shares one L3.

## Correctness checks
- `-Wall -Wextra -Wpedantic`: no warnings
- ThreadSanitizer (`-fsanitize=thread`): no data race reports
- AddressSanitizer + UBSan (`-fsanitize=address,undefined`): clean
- all runs below: `ordered=OK`, pushed == popped

## Results: objects pushed and popped in 1 second (unpinned, 5 runs)

| Run | std::mutex | spinlock | lock-free |
|----:|-----------:|---------:|----------:|
| 1 | 2,194,280 | 155,870   | 13,370,918 |
| 2 | 2,807,031 | 376,745   | 13,015,843 |
| 3 | 2,271,375 | 638,637   | 10,759,901 |
| 4 | 2,170,627 | 1,532,434 | 13,331,771 |
| 5 | 1,853,359 | 1,938,358 | 14,210,462 |
| **Median** | **2,194,280** | **638,637** | **13,331,771** |
| **Mean**   | **2,259,334** | **928,409** | **12,937,779** |

Mean throughput (64 B per object): mutex 2.26 M ops/s (0.14 GB/s),
spinlock 0.93 M ops/s (0.06 GB/s), lock-free 12.94 M ops/s (0.83 GB/s).

**Answer to "how many 64-byte objects per second with locks":
about 2.2 million/s with `std::mutex`, and somewhere between 0.16 and 1.9 million/s
with the spinlock (median 0.64 million/s).**

## Pinned runs (taskset, one run each)

| Threads pinned to | std::mutex | spinlock | lock-free |
|---|---:|---:|---:|
| `-c 0,2` different physical cores | 2,406,697 | 496,821   | 10,549,296 |
| `-c 0,1` same physical core (HT siblings) | 2,285,360 | 2,999,161 | 33,397,088 |

## What I noticed
- Lock-free is about 6x the mutex and about 21x the spinlock (medians, unpinned).
  Each op only touches one shared index, no lock to fight over.
- The spinlock is the weird one. Unpinned it swings from 0.16M to 1.94M between runs,
  and on separate cores it is much slower than the mutex. When both threads sit on
  the same physical core it jumps to 3.0M and beats the mutex. My guess: it is
  cache line ping-pong. Both threads hammer the lock word with `test_and_set`,
  and when they are on different cores that line has to travel between cores through L3
  on every attempt. Hyperthread siblings share L1, so the line never leaves.
  The unpinned spread would just be the OS placing the two threads differently each run.
- The lock-free queue shows the same effect: 10.5M on separate cores vs 33.4M on siblings.
- The mutex barely cares where the threads run (2.3 to 2.4M both ways), probably because
  a waiting thread sleeps instead of hammering the line.
- Caveats: the pinned numbers are single runs, and I did not use perf to confirm the cache
  misses, so the ping-pong explanation is a hypothesis that fits the data, not something I proved.
- Possible spinlock fixes I did not try: spin on a plain load before `test_and_set`
  (test-and-test-and-set), a `pause` in the loop, backoff.
