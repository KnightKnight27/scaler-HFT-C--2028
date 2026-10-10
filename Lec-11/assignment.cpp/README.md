# SPSC Queue Assignment

**Name:** Khushi Sarawagi  
**Roll No:** 24bcs10361  
**Email:** khushi.24bcs10361@sst.scaler.com  

For this assignment I implemented a Single Producer Single Consumer queue and compared different ways of synchronizing it.

The queue stores 64 byte objects and has a fixed capacity of 1024.

I implemented 3 versions:

- `std::mutex`
- custom spinlock
- lock-free queue using atomics

The producer thread keeps pushing objects and the consumer thread keeps popping them.

For testing, I used 10,000,000 operations and measured how much time each version takes.

## Build

```bash
g++ -O2 -std=c++17 -pthread spsc_queue.cpp -o spsc
```

## Run

Run all versions:

```bash
./spsc
```

Run only spinlock:

```bash
./spsc spin
```

Run only mutex:

```bash
./spsc mutex
```

Run only atomic queue:

```bash
./spsc atomic
```

## Result

Running all three together:

```text
Mutex Queue: 0.24294 sec, 41162349 objects/sec
SpinLock Queue: 0.201455 sec, 49638918 objects/sec
Atomic Queue: 0.337548 sec, 29625372 objects/sec
```

Running each version separately:

```text
SpinLock Queue: 0.221298 sec, 45187851 objects/sec
Mutex Queue: 0.27007 sec, 37027488 objects/sec
Atomic Queue: 0.318912 sec, 31356641 objects/sec
```

The results change between runs depending on CPU load and other processes running on the system.

In my tests, the spinlock version gave the highest throughput, followed by the mutex version. The atomic queue was slower in these runs.

## Implementation

For the normal queue I used `writeIndex` and `readIndex`.

When:

```cpp
writeIndex - readIndex == capacity
```

the queue is full.

When:

```cpp
writeIndex == readIndex
```

the queue is empty.

The queue size is kept as a power of 2, so instead of:

```cpp
index % capacity
```

I used:

```cpp
index & (capacity - 1)
```

For the spinlock version, an atomic boolean keeps trying until the lock becomes available.

The atomic version does not use a lock. The producer updates the write index and the consumer updates the read index.

I also aligned the atomic indices to 64 bytes to reduce the chance of them sharing the same cache line.

## Observation

The mutex version is simple and works well, but it has locking overhead.

The spinlock version does not block the thread, so for this small benchmark it performed better on my machine.

The atomic version avoids a lock, but in my test it was not the fastest. This shows that lock-free does not always automatically mean faster, and the result also depends on the machine and workload.