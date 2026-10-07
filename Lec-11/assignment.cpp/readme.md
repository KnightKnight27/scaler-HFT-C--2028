# SPSC Queue

email: "seeta.24bcs10250@sst.scaler.com"

roll_no: "24bcs10250"


Simple SPSC queue in C++ using a fixed-size ring buffer. Uses `std::mutex` to keep `push()` and `pop()` safe across threads.

The benchmark runs a producer and consumer for 1 second with 64-byte messages to test transfer rate and throughput in MiB/s.

Added unit tests to check FIFO ordering, empty/full conditions, and wrap-around.

## Benchmark Result

![SPSC Queue Benchmark](image.png)

