# SPSC Queue

Implemented a basic Single Producer Single Consumer (SPSC) queue in C++ using a fixed-size ring buffer.  
The queue uses `std::mutex` to safely synchronize `push()` and `pop()` operations between two threads.  
A producer thread pushes 64-byte objects while a consumer thread removes them for 1 second.  
The benchmark measures the number of successful object transfers per second and the total throughput in MiB/s.  
Basic tests are also included to verify queue capacity, FIFO ordering, empty/full behavior, and wrap-around.

## Benchmark Result

![SPSC Queue Benchmark](image.png)