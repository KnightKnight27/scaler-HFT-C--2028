email: "rudhar.24bcs10143@sst.scaler.com"

roll_no: "10143"

SPSC queue as a ring buffer (preallocated array of 64-byte objects) guarded by `std::mutex`. The producer thread pushes and the consumer thread pops for 1 second, then `t1.join()` / `t2.join()`.

Build: `clang++ -std=c++20 -O2 -pthread spsc_queue.cpp -o spsc && ./spsc`

Results (queue size 1024, `-O2`, Apple M5 Pro, 3 runs):

| Run | Pushed / s | Popped / s |
|---|---|---|
| 1 | 15,146,772 | 15,145,775 |
| 2 | 13,354,070 | 13,353,047 |
| 3 | 12,466,535 | 12,466,470 |

About **12–15 million 64-byte objects per second** (~0.8–1 GB/s) through the queue with a mutex.
