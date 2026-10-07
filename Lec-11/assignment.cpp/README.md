# SPSC Queue assignment

single producer single consumer queue. 1 thread pushes 64 byte objects, other
thread pops them. used `std::mutex` to lock the shared `std::queue` (not
spinlock, going with mutex since thats what i learned so far).

producer runs for 1 second and keeps pushing as fast as it can, consumer keeps
popping until producer is done AND queue is empty. counted how many pushes /
pops happened using atomic counters.

## how to run

```
g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

## my per second numbers

tested on Apple M1, ran it 3 times:

| run | pushed | popped |
|-----|--------|--------|
| 1   | 2,708,311 | 2,708,311 |
| 2   | 2,607,928 | 2,607,928 |
| 3   | 2,742,096 | 2,742,096 |

so roughly **~2.6 - 2.7 million** 64 byte objects per second with a
`std::mutex` lock around the queue. push count == pop count every time since
consumer drains everything before exiting.

## notes / things i'd try next

- havent tried the spinlock (`while(true)` busy wait) version yet, that
  should probably be faster than mutex since no OS context switch, want to
  compare numbers later
- using `std::queue` means every push/pop does a heap alloc/dealloc
  internally, a ring buffer (fixed size array) would probably be a lot faster
- lock is held for a pretty small critical section already so not sure how
  much more headroom there is without going lock-free
