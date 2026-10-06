# Lec 11 assignment

This is a bounded SPSC queue protected by `std::mutex`.

Build and run:

```bash
g++ -std=c++17 -O2 -pthread assignment.cpp/spsc_queue.cpp -o spsc_benchmark
./spsc_benchmark
```

One run with 64-byte payloads and a capacity of 1024 produced:

```text
pushes/sec: 6125926
pops/sec:   6125872
```

The exact result depends on CPU load and scheduling.
