# SPSC Queue Assignment

This folder contains two simple producer-consumer implementations for studying single-producer single-consumer (SPSC) queues in C++.

## Files

- `spsc_queue.cpp` — a queue implementation using a custom spin lock
- `spsc_mutex.cpp` — a queue implementation using `std::mutex`

## Purpose

These examples demonstrate:

- fixed-capacity queue design
- producer/consumer synchronization
- ring-buffer behavior
- thread coordination in C++

## Build and run

Compile any file with:

```bash
g++ -std=c++11 -Wall -Wextra -Wpedantic -pthread spsc_queue.cpp -o spsc_queue
./spsc_queue
```

or

```bash
g++ -std=c++11 -Wall -Wextra -Wpedantic -pthread spsc_mutex.cpp -o spsc_mutex
./spsc_mutex
```

## Notes

The examples are intended for learning and demonstration. They are not production-grade performance locks or queues.
