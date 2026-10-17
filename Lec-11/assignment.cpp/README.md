# SPSC Queue (Single Producer Single Consumer)

## Author Information
- Name: Abhiroop Sistu
- Email: abhiroop.24bcs10287@sst.scaler.com
- Roll Number: 24bcs10287

## Overview
This program implements an SPSC queue in C++ using `std::mutex`.

The queue uses a circular buffer of size 1024. The producer pushes 64-byte objects into the queue, and the consumer pops them. A mutex is used to protect the queue during these operations.

## How to Run

Compile:
```bash
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc_queue
```

Run:
```bash
./spsc_queue
```

## Benchmark Results

The program was run three times, transferring 5,000,000 objects in each run.

| Run | Time (seconds) | Objects/second |
|---|---:|---:|
| 1 | 0.951241 | 5,256,290 |
| 2 | 0.983863 | 5,082,010 |
| 3 | 0.961009 | 5,202,860 |

Average throughput was approximately **5.18 million objects per second**. Counting both push and pop operations, the average was around **10.36 million operations per second**.

## Correctness Check

All three runs transferred the full 5,000,000 objects and produced the expected checksum:

```text
12499997500000
```

The consumer also checks that objects are received in the correct order.

## Conclusion

The queue passed all three tests, with an average throughput of about 5.18 million objects per second.