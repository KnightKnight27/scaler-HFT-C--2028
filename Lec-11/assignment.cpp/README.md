
# SPSC Queue: spinlock vs std::mutex

**Name:** <your name> | **Roll no:** <roll no> | **Email:** <email>**Name:** Swarnika somvanshi | **Roll no:** 10482 | **Email:** swarnika.24bcs10482@sst.scaler.com

## what is this

one thread (producer) keeps shoving 64-byte items into a queue, another thread (consumer) keeps pulling them out. both of them are fighting over the same lock. i ran it for 1 second with a spinlock, then 1 second with `std::mutex`, and counted how many items made it through.

## how it works (the simple version)

- **the item:** 8 byte counter (`seq`) + 56 bytes of padding = exactly 64 bytes. `alignas(64)` makes each one sit on its own cache line, and a `static_assert` yells at compile time if it isn't 64.
- **memory pool:** one array of 1024 slots, made once, reused forever. no `new`/`malloc` on every push.
- **ring buffer:** `head` is where the consumer reads, `tail` is where the producer writes. both wrap back to 0 with `% CAP`, like a conveyor belt that loops.

```
slots:  [0][1][2][3] ... [1022][1023]
          ^head              ^tail  -> after 1023 it wraps back to 0
```

- **locks:** my own `SpinLock` (an `atomic_flag` + a `while(test_and_set)` loop) vs `std::mutex`. the queue is a template, so the lock type is the blank i fill in: `Queue<SpinLock>` or `Queue<std::mutex>`.
- **order check:** the producer stamps `seq` = 0, 1, 2... the consumer checks they come out in the same order. if not, it prints `NO`.
- **timing:** main sleeps 1 sec, flips a `stop` flag, then `join()`s both threads. i divide by the real measured time, since `sleep_for` is never exactly 1 sec.

## build and run

```
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o /tmp/spsc
/tmp/spsc
```

## my machine

- laptop: ASUS Vivobook
- CPU: <paste from `lscpu | grep "Model name"`>
- OS: <paste from `lsb_release -d`>
- compiler: <paste from `g++ --version | head -1`>
- flags: `-O2 -std=c++17 -pthread`

## results

one run:

| lock | pushed | popped | ops/sec | order ok |
|---|---|---|---|---|
| spinlock | 3,459,767 | 3,458,744 | 3,457,094 | yes |
| std::mutex | 2,057,127 | 2,056,105 | 2,055,211 | yes |

5 runs, ops/sec:

| run | spinlock | std::mutex |
|---|---|---|
| 1 |3470726 |1877171 |
| 2 |2214100 |1853220 |
| 3 |3718383 |1912079 |
| 4 |3610893 |1823369 |
| 5 |3553401 |1843668 |

## what i noticed

- spinlock was about **1.7x faster** than the mutex in the first run.
- pushed is always ~1022-1023 more than popped. the queue only has 1024 slots, so that means it was almost full when i hit stop. the producer is faster than the consumer, and the leftovers just never got popped.
- **why i think spinlock won (a guess, i didn't test it):** the stuff inside the lock is tiny (copy one item, move one index). so waiting a few nanoseconds by spinning is cheaper than the mutex's way of putting a thread to sleep and waking it up through the OS.

## limits / honest stuff

- one laptop, numbers change a bit every run.
- it's a lock-based queue, so both threads keep bumping into each other on the same lock. i didn't make a lock-free version.
- spinlock burns 100% of a CPU core while waiting, the mutex doesn't. throughput isn't the only thing that matters.
