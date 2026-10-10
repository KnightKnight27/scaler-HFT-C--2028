# SPSC Queue - Snehangshu Roy (24bcs10155)

Ring buffer queue with one producer and one consumer. The buffer is allocated once
at startup and reused (basically a fixed memory pool), so there's no allocation
while the benchmark runs. Every push/pop takes a lock, I tried two:

- `SpinLock` - `std::atomic_flag` with a while loop
- `std::mutex`

Each object is a 64 byte struct (`alignas(64)`, a sequence number + padding).
The producer and consumer run for 1 second, then I print how many got pushed/popped.
The consumer also checks the sequence numbers come out in order.

## Build / run

```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc
./spsc
```

## Per second specs

Machine: AMD Ryzen 9 8940HX (16C/32T), Windows 11, g++ (MSYS2 ucrt64), -O2

| lock     | queue capacity | pops / sec | MB / sec |
|----------|---------------:|-----------:|---------:|
| spinlock | 64             | ~12.8 M    | ~780     |
| mutex    | 64             | ~4.4 M     | ~270     |
| spinlock | 1024           | ~13.6 M    | ~830     |
| mutex    | 1024           | ~26.3 M    | ~1600    |
| spinlock | 65536          | ~14.6 M    | ~890     |
| mutex    | 65536          | ~43.6 M    | ~2660    |

(average of 2 runs, numbers move around a bit between runs)

## Notes

- With a small queue the spinlock wins easily. The queue is full/empty most of the
  time so the threads keep fighting over the lock, and mutex ends up sleeping/waking.
- With a bigger queue the mutex actually came out faster, which I didn't expect.
  My guess is that once a thread gets the mutex it gets to run a bunch of push/pops in
  a row while the other one is parked, so there's less cache line bouncing. The spinlock
  just keeps ping-ponging the flag between the two cores.
- Either way a lock on every operation is the bottleneck. A lock free version with
  atomic head/tail would be a lot faster but the assignment was to do it with locks.
