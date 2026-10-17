email: "shreyash.24bcs10253@sst.scaler.com"

roll_no: "10253"

# Lec 11 assignment - SPSC queue with locks

One producer thread (t1) pushes 64 byte packets, one consumer thread (t2) pops them.
Main sleeps for 1 sec, sets `done = true`, then `t1.join()` and `t2.join()` and prints how many got through.

## what's inside

- `Packet` struct = 8 byte seq number + 56 byte payload = 64 bytes (checked with static_assert)
- queue is a ring buffer of 1024 slots, array is allocated once in the constructor so no
  new/delete while pushing and popping (this is the memory pool part)
- queue is templated on the lock so same code runs with `std::mutex` and my `SpinLock`
- spinlock is an `atomic<bool>` with `exchange` in a while loop (and it spins on a plain load
  while locked so it doesn't spam exchange all the time)
- consumer checks that seq numbers come out in order

## run

```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc
./spsc
```

## per second specs

Laptop: Intel i5-1335U (13th gen), Ubuntu 24.04, g++ 13.3, `-O2`

| run | std::mutex (pops/sec) | spinlock (pops/sec) |
|-----|-----------------------|---------------------|
| 1   | 2,310,561             | 2,993,595           |
| 2   | 2,266,105             | 1,490,839           |
| 3   | 2,828,273             | 3,405,516           |
| 4   | 2,796,879             | 3,808,103           |

so roughly:

- **std::mutex : ~2.5 million 64B objects / sec (~150 MB/s)**
- **spinlock   : ~3 million 64B objects / sec (~180 MB/s)** but it jumps around a lot

pushed is sometimes a bit more than popped, thats just the stuff still sitting in the queue when it stopped.

## notes

- spinlock was faster in most runs but run 2 was really bad. i think its because this cpu has
  P cores and E cores and if one of the threads lands on an E core the spinning wastes time.
- both are pretty slow because push and pop fight for the same lock every single time. a lock free
  version with atomic head/tail would be way faster, maybe next time.
