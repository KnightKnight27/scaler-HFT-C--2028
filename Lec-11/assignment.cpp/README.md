# SPSC queue

email: "mohammed.24bcs10278@sst.scaler.com"

roll_no: "24bcs10278"

Single producer / single consumer queue over a preallocated ring buffer, guarded by a lock
(std::mutex or spinlock). Producer and consumer push/pop 64-byte objects for 1 second, then join.

Build and run:

```
g++ -O2 -std=c++17 -pthread -static spsc_queue.cpp -o spsc_bench
./spsc_bench
```

64-byte objects pushed and popped per second (ring capacity 1024, 20-core Windows 11, g++ -O2):

| Lock       | objects/sec |
|------------|------------:|
| std::mutex | 6034152 |
| spinlock   | 3621141 |
