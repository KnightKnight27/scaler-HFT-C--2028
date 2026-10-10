# spsc queue with locks

one producer thread pushes 64 byte objects, one consumer thread pops them. the queue is a ring buffer and every push/pop takes a lock. tried it with:

- spinlock (while loop doing compare exchange on an atomic bool)
- std::mutex
- no lock at all, just atomics (the lec-12 one), only to see how much the lock costs

the buffer is one block allocated once at the start (4096 slots * 64 bytes = 256 kb), that is the memory pool, so no new/delete while it is running.

things from the lec-10 notes that i used:

- size is a power of 2 so `idx & (size - 1)` instead of modulo
- push and pop idx just keep incrementing, size is `pushIdx - popIdx`
- `[[unlikely]]` on the full / empty checks
- pop takes `T&` so there is no extra copy
- acquire / release memory order instead of the default
- in the atomic one both idx are `alignas(64)` so they dont sit on the same cache line

the consumer also checks that objects come out in the same order and the payload is not corrupted.

## build and run

```
g++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## machine

- amd ryzen 7 7840hs (8 cores / 16 threads), laptop, plugged in
- linux 7.1.8 (cachyos), governor was powersave
- g++ 16.2.1

## results (objects pushed and popped in 1 second)

`-O2`, 3 runs:

| run | spinlock | std::mutex | atomic (no lock) |
|-----|----------|------------|------------------|
| 1   | 10.32 M  | 7.30 M     | 33.08 M          |
| 2   | 14.12 M  | 7.80 M     | 32.65 M          |
| 3   | 10.14 M  | 7.47 M     | 33.20 M          |

`-O0`, 1 run:

| spinlock | std::mutex | atomic (no lock) |
|----------|------------|------------------|
| 6.89 M   | 5.57 M     | 13.13 M          |

so with locks i get around 10-14 million objects per second with the spinlock and around 7-8 million with std::mutex on my laptop. without the lock it is around 33 million.

## notes

- spinlock beats the mutex here. the critical section is tiny (copy 64 bytes and bump an index) and there are only 2 threads, so spinning is cheaper than what the mutex does when it is contended.
- spinlock numbers jump around the most between runs, threads are not pinned to cores.
- the atomic one is 2-3x the spinlock because the two threads never wait on each other, each one only writes its own index.
- also ran it with `-fsanitize=thread`, no data races reported.
