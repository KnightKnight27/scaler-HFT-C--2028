email: "prateek.24bcs10135@sst.scaler.com"

roll_no: "10135"

# SPSC Queue (Lec 11 assignment)

One producer thread pushes 64 byte objects into a queue and one consumer thread pops them.
Both run for 1 second and then I count how many went through.

Code is in `spsc_queue.cpp`.

## What I did

- `Obj` struct is exactly 64 bytes (8 byte id + 56 byte char array)
- Queue is a ring buffer of 1024 slots. The array is made once in the constructor so there is
  no `new` / `delete` during push and pop (kind of a fixed memory pool)
- The queue is a template on the lock type, so I ran it with `std::mutex` and with my own
  spinlock (`atomic_flag` in a while loop)
- t1 = producer, t2 = consumer, main sleeps 1 sec, sets `stop`, then `t1.join()` and `t2.join()`
- Consumer checks that ids come out in the same order they went in

## How to run

```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## Per second specs

Machine: Apple M5 Pro (macOS), compiled with `-O2`

Ran it 3 times:

| Run | std::mutex (objects/sec) | spinlock (objects/sec) |
|-----|--------------------------|------------------------|
| 1   | 13,011,126               | 5,100,728              |
| 2   | 14,409,201               | 4,890,371              |
| 3   | 15,097,589               | 5,428,998              |

So roughly:

- **std::mutex: ~14 million 64B objects per second** (~880 MB/s)
- **spinlock: ~5 million 64B objects per second** (~310 MB/s)

Output from one run:

```
sizeof(Obj) = 64 bytes

std::mutex
  pushed in 1 sec : 15097589
  popped in 1 sec : 15097589
  MB/s popped     : 921
  order ok

spinlock
  pushed in 1 sec : 5429022
  popped in 1 sec : 5428998
  MB/s popped     : 331
  order ok
```

## Notes

I thought spinlock would be faster but on my Mac the mutex won by about 3x. My guess is that
both threads keep hammering the same `atomic_flag` with `test_and_set`, so the cache line
keeps bouncing between the two cores. The mac mutex is already pretty well optimised for
short critical sections like this one. Numbers will probably be different on Linux / x86.
