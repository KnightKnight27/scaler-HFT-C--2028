# EMAIL
himanshu.24bcs10071@sst.scaler.com

# ROLL NUMBER
24bcs10071




# SPSC Queue

This is my assignment for a single producer single consumer queue in C++.
One thread puts objects in the queue and another thread takes them out.
The queue uses a lock so the two threads don't mess up each other.
I tried two kinds of locks, `std::mutex` and a spinlock, to see which one is faster.

## How it works

- The queue is a ring buffer. It is just an array with a `head` and a `tail` number. When they reach the end of the array they go back to 0.
- Each object is 64 bytes. I used a struct with an id and some padding. The id is there so the consumer can check that things come out in the same order they went in.
- The array is made one time at the start and reused. I don't use `new` or `delete` while the test is running. This is the memory pool part of the assignment.
- The spinlock is a `std::atomic_flag` in a while loop. It keeps trying until it gets the lock.
- The producer thread keeps pushing and the consumer thread keeps popping. If the queue is full or empty they just try again.
- `main` waits 1 second, sets a stop flag, then calls `join()` on both threads.

## How to run

You need g++ and make.

```
make run
```

or by hand:

```
g++ -O2 -std=c++17 -pthread main.cpp -o bench
./bench
```

Use `-O2`. Without it the numbers are a lot lower.

## Results

Each test runs for 1 second and counts how many 64 byte objects the consumer popped.

My computer: (cpu name: 13th Gen Intel(R) Core(TM) i5-13420H, number of cores: 8, os: windows)
Compiler: (g++ version)


| lock       | run 1      | run 2      |
|------------|------------|------------|
| std::mutex | 15,647,194 | 13,772,494 |
| spinlock   | 3,786,389  | 4,563,909  |

## What I noticed

(write what you saw here, like which one was faster and if the numbers changed between runs)

## Things that could be better

- The code is simple and not very clean, I just wanted it to work first.
- The numbers change a little each time you run it, so I would run it a few times.
- Both threads fight for the same lock all the time, so a lock free queue should be faster. I haven't done that yet.
- The spinlock can be really slow if the computer has only 1 core, because the waiting thread burns its time slice doing nothing.
