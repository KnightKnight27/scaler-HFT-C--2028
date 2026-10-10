# Bounded SPSC queue

email: "pratham.24bcs10136@sst.scaler.com"

roll_no: "10136"

This assignment implements a bounded single-producer/single-consumer channel
backed by one pre-allocated ring of objects. The same queue is exercised with
two lock policies: the standard library mutex and a small atomic busy lock.

## Layout

- `spsc_queue.hpp` — queue and lock-policy implementation
- `spsc_queue.cpp` — one-second transfer benchmark using 64-byte market events
- `test.cpp` — FIFO, capacity, wrap-around, and two-thread checks
- `image.png` — captured benchmark output

`MarketEvent` is aligned and fixed at 64 bytes. Every event carries a sequence
number, allowing the consumer to detect reordering while the benchmark runs.

## Build

Run from the `Lec-11` directory:

```bash
g++ -std=c++17 -pthread assignment.cpp/test.cpp -o /tmp/spsc_tests
/tmp/spsc_tests

g++ -O2 -std=c++17 -pthread assignment.cpp/spsc_queue.cpp -o /tmp/spsc_benchmark
/tmp/spsc_benchmark
```

The reported throughput varies with CPU scheduling and machine load.

## Benchmark screenshot

![SPSC benchmark output](image.png)
