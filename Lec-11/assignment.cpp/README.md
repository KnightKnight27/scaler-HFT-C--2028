Benchmark Results

Object size: 64 bytes
Queue capacity: 1024
Objects transferred: 5000000
Data validation: PASSED
Elapsed time: 2.23285 seconds
Throughput: 2.23929e+06 objects/second

Synchronization

The queue uses std::mutex to protect its shared buffer and queue indices. The producer and consumer retry when the queue is full or empty.

Limitations

This implementation prioritizes correctness and simplicity. Mutex acquisition and retrying can affect throughput.