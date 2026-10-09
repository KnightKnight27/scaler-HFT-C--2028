# SPSC Queue Benchmark: Mutex vs Spinlock

## Overview

This assignment implements a Single Producer, Single Consumer (SPSC)
queue in C++. A producer thread pushes fixed-size objects into the
queue, while a consumer thread pops them.

Two locking approaches are implemented and compared:

-   **Mutex version** --- uses `std::mutex` to protect queue operations.
-   **Spinlock version** --- uses a custom `SpinLock` built with
    `std::atomic_flag` and a `while` loop.

Both versions use a memory pool to allocate storage for queue slots in
one allocation and reuse those slots as objects are pushed and popped.

## Queue and test setup

  Setting                                                 Value
  --------------------------- ---------------------------------
  Object type                                         `Message`
  Object size                                          64 bytes
  Queue capacity                                  1,024 objects
  Producer threads                                            1
  Consumer threads                                            1
  Benchmark duration target                            1 second
  Synchronization               `std::mutex` or custom spinlock

The code verifies the object size at compile time using
`static_assert(sizeof(Message) == 64, ...)`.

## Benchmark results

The following results are from one run on the same machine. They are
observations, not guaranteed performance figures.

  Metric                    Mutex implementation         Spinlock implementation
  ------------------------ ---------------------- ---------------------
  Successful pushes          877,160                     1,277,430
  Successful pops            877,155                     1,277,427
  Reported push throughput   876,846 objects/s       1,276,940 objects/s
  Reported pop throughput    876,841 objects/s       1,276,930 objects/s

### Comparison

From the results above, the spinlock implementation is performing better than the mutex implementation. It is processing around **1.46 times more objects per second**, which is approximately **45.6% more throughput** in this run.

The reason for this could be that the spinlock uses a simple `while` loop to keep checking whether the lock is available. Since our push and pop operations are relatively short, the lock might become available quickly, making this approach faster in our case.

In the mutex implementation, the locking is handled by `std::mutex`, which may involve additional overhead. However, this does not mean that spinlocks are always better than mutexes. A spinlock keeps using CPU while waiting for the lock, whereas a mutex can let a waiting thread sleep. So, if the waiting time is longer, a spinlock might actually perform worse.

For this assignment, the spinlock gave better results in my test, but the performance can vary depending on the system and how the threads are scheduled.

## Notes

* These results are from one run, so the numbers may change if I run the programs again.
* The timer starts before the threads are created, and the elapsed time is calculated after both threads finish. So, the throughput is an approximate measurement.
* The number of pushes and pops is slightly different because the producer and consumer can stop at different points, leaving a few objects in the queue.
* To get a better idea of the performance, both programs can be run multiple times and their results compared.
* Each object is 64 bytes, and the throughput is measured in objects per second. Multiplying the throughput by 64 gives an approximate data rate in bytes per second, excluding other overheads.

## Build and run

Compile each implementation with the same flags from the assignment
directory:

``` bash
g++ -std=c++17 -O2 -pthread spsc_queue.cpp -o spsc_queue
g++ -std=c++17 -O2 -pthread spsc_queue_spinlock.cpp -o spsc_queue_spinlock
```

Run them separately:

``` bash
./spsc_queue
./spsc_queue_spinlock
```

For a fair comparison, use the same compiler options for both
implementations and record results from repeated runs.

## Summary

-   Both implementations use a fixed-capacity SPSC queue and protect
    queue operations with a lock.
-   The mutex implementation is straightforward and lets the operating
    system manage waiting.
-   The spinlock implementation repeatedly checks the lock and may be
    faster for short critical sections, but it can consume CPU while
    waiting.
-   In the observed run, the spinlock showed approximately 45.6% higher
    reported throughput. More repeated measurements are needed before
    drawing a general conclusion.