Student details
email: rama.24bcs10087@sst.scaler.com
roll_no: 24BCS10087
SPSC Queue (With Locks, std::mutex and not spinlock)
This is a C++ implementation of a SPSC (Single Producer, Single Consumer) queue which uses std::mutex.

Please note that this is not a lock-free implementation, and thus is not very good performance wise.

The actual SPSCQueue data structure is in spscqueue.h.

main.cpp only contains code that uses that class to perform as many push/pops as possible under the provided constraints.

Benchmark
The target binary (./SPSCQueue) can be provided with two parameters:

Milliseconds allowed: Time in milliseconds before all threads terminate execution.
Number of threads: How many threads should try to push/pop from the queue at once.
There is another file called benchplot.py which runs the program from 1 to 10 threads with 1 second allowed and then plots a nice bar chart using matplotlib.

The bar chart shows that the total number of operations (push + pop) are generally inversely proportional to the number of threads we use.

This is logical since multiple threads mean multiple lock() / unlock() calls and each of them sleeps the thread for some period and wakes them up again to check the lock. This is slightly expensive.

Benchmark image

Future improvements
A spin lock seems better for this usecase because it doesn't cause the thread to sleep like what std::mutex does. And we have some work to do constantly so a spin lock should be a better choice.

A lock-free implementation should be much much better.
