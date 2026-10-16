# SPSC Queue + Fixed-Size Memory Pool Benchmark

**Author:** Harsha Shetty
**Roll number:** 24BCS10208

## Goal

Implement and benchmark a single-producer, single-consumer (SPSC) queue. One thread repeatedly pushes fixed-size objects while another thread pops them. The benchmark compares two locking strategies:

- `std::mutex`
- A simple `std::atomic_flag` spinlock

Each object is exactly **64 bytes**. The queue allocates its ring buffer once at startup and reuses those slots, so push/pop operations do not dynamically allocate memory in the hot path. This preallocated ring buffer acts as the fixed-size object pool for this benchmark.

## Build

Requires a C++17 compiler and pthread support (Linux/macOS with GCC or Clang):

```bash
g++ -O2 -std=c++17 -pthread spsc_bench.cpp -o spsc_bench
```

Optional: run with additional warnings while developing:

```bash
g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic -pthread spsc_bench.cpp -o spsc_bench
```

## Run

Default: ring capacity of 1,024 objects and a 1,000 ms measurement window.

```bash
./spsc_bench
```

Custom capacity and duration (milliseconds):

```bash
./spsc_bench 4096 1000
```

The benchmark prints successful pushes and pops, their rates per second, queue-full/queue-empty attempts, and a FIFO sequence check. It runs the mutex and spinlock tests separately.

## Results

### Test Environment

- **CPU:** Intel Core i7-10610U @ 1.80 GHz
- **Logical CPUs:** 8
- **Operating System:** Pop!_OS 24.04 LTS
- **Compiler:** GCC 13.3.0 (`Ubuntu 13.3.0-6ubuntu2~24.04.1`)
- **Build flags:** `-O2 -std=c++17 -Wall -Wextra -Wpedantic -pthread`
- **Ring capacity:** 1,024 objects
- **Measurement duration:** 1 second per lock type per run
- **Number of runs:** 4

### Benchmark Results

The following results are the median throughput across four runs on the test machine. Throughput is reported separately for successful pushes and pops.

| Lock type | Pushed objects/sec (median) | Popped objects/sec (median) | FIFO check |
|---|---:|---:|---|
| `std::mutex` | 3,977,193 | 3,976,173 | OK |
| `std::atomic_flag` spinlock | 2,081,520 | 2,080,498 | OK |

### Observed Throughput Range

| Lock type | Pushes/sec | Pops/sec |
|---|---:|---:|
| `std::mutex` | 3,747,022–4,266,731 | 3,746,434–4,265,726 |
| Spinlock | 646,875–4,549,497 | 645,861–4,549,117 |

### Observations

- Both locking implementations passed the FIFO ordering check in all four runs.
- The median results show higher throughput for `std::mutex` than for the spinlock in these measurements.
- Spinlock throughput varied considerably between runs. Scheduling, CPU load, power management, and contention can affect performance.
- Queue-full and queue-empty failures represent unsuccessful attempts, not successful object transfers.
- The number of successful pushes may differ slightly from the number of successful pops because the queue can contain unconsumed objects when the measurement stops.
- These results describe this machine and these test conditions; they are not a universal ranking of mutexes and spinlocks.