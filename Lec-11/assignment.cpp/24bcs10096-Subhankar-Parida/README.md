# Lec-11 SPSC Queue Assignment: 1-Second Benchmark

## Author Details
- **Name:** Subhankar Parida
- **Roll Number:** 24bcs10096
- **Email:** subhankar.24bcs10096@sst.scaler.com / subhankarparida20094013@gmail.com

---

## 📌 Problem Statement & Requirements
1. Implement a bounded **Single-Producer Single-Consumer (SPSC)** Queue.
2. Synchronize push and pop operations using locks:
   - **`SpinLock`** (`while` loop with atomic test-and-set and CPU yield hint)
   - **`std::mutex`** (mutual exclusion)
3. Use two threads: producer (`t1`) and consumer (`t2`), ensuring proper lifecycle with `t1.join()` and `t2.join()`.
4. Measure **how many 64-byte objects can be pushed and popped in 1 second**.
5. Use a **Memory Pool** to eliminate heap allocations (`malloc`/`free`) from the hot path.

---

## 🛠️ Architecture & Design

### 1. 64-Byte Object (`Object`)
The payload is padded and aligned to 64 bytes (`alignas(64)`), which matches a single CPU cache line:
```cpp
struct alignas(64) Object {
    uint64_t sequence{0};
    uint64_t timestamp{0};
    uint8_t  payload[48]{0};
};
static_assert(sizeof(Object) == 64, "Object must be exactly 64 bytes");
static_assert(alignof(Object) == 64, "Object must be 64-byte cache-line aligned");
```

### 2. Synchronization Mechanisms
- **`SpinLock`**: Uses `std::atomic_flag` with acquire/release semantics in a `while` loop, with architecture-specific CPU yield hints (`asm volatile("yield")` on ARM64, `_mm_pause()` on x86_64).
- **`std::mutex`**: Standard library mutual exclusion using RAII `std::lock_guard`.

### 3. Memory Pool Architecture
The SPSC queue pre-allocates an arena buffer (`std::vector<Object> pool_`) at startup. Slots are accessed using circular bitwise index masking (`tail_ & (capacity - 1)`). This ensures:
- **Zero dynamic memory allocations** (`malloc`/`new`/`free`) during the 1-second benchmark window.
- Sequential cache line traversal with zero pointer chasing.

### 4. Producer & Consumer Lifecycle
- `t1` (Producer) writes objects with sequential sequence IDs into the queue under lock.
- `t2` (Consumer) pops objects under lock and verifies strict FIFO ordering.
- The main thread coordinates an exact 1.00-second window, triggers completion, drains remaining items, and joins both threads:
  ```cpp
  t1.join();
  t2.join();
  ```

---

## 🚀 Per-Second Specs & Benchmark Results

**Environment:** Apple M3 (ARM64, 8 Cores), macOS Darwin, Apple Clang 17.0.0 (`-std=c++17 -O3 -pthread`).

| Capacity | Lock Policy | Objects Pushed (1s) | Objects Popped (1s) | Throughput (M ops/s) | Bandwidth (MB/s) | FIFO Status |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **65,536** | `std::mutex` | **51,221,125** | **51,221,125** | **50.97 M ops/s** | **3,110.7 MB/s** | **PASSED** |
| **65,536** | `SpinLock` | **23,111,685** | **23,111,685** | **23.00 M ops/s** | **1,403.6 MB/s** | **PASSED** |
| **1,024** | `std::mutex` | **30,023,524** | **30,023,524** | **29.87 M ops/s** | **1,823.4 MB/s** | **PASSED** |
| **1,024** | `SpinLock` | **10,272,962** | **10,272,962** | **10.22 M ops/s** | **623.9 MB/s** | **PASSED** |

### Summary of Specs:
- **Peak Throughput with Locks:** **51.22 Million 64-byte objects/sec** (over **3.1 GB/s**) using `std::mutex` and 64K-slot memory pool.
- **SpinLock Peak:** **23.11 Million 64-byte objects/sec** (~**1.4 GB/s**).
- **Data Integrity:** 100% verified FIFO sequence matching (0 lost, 0 corrupted objects).

---

## 🔍 Observations & Analysis
1. **Why `std::mutex` Outperformed `SpinLock`:**
   On Darwin/macOS, `std::mutex` leverages `os_unfair_lock`, an adaptive hybrid lock. When one thread holds the lock, the operating system can briefly park the competing thread, allowing the active thread to execute consecutive bursts of push/pop operations with reduced inter-core cache line bouncing. In contrast, pure busy-waiting with `SpinLock` forces continuous atomic write snooping across CPU core caches, degrading overall throughput under heavy lock contention.
2. **Impact of Queue Capacity (Memory Pool Sizing):**
   Increasing queue capacity from 1,024 to 65,536 increased throughput by **~70% to 125%** because larger buffering reduces the frequency of threads encountering full or empty states.

---

## 💻 Build & Execution Instructions

From the repository root:

```bash
# Compile with optimizations
clang++ -std=c++17 -O3 -pthread -Wall -Wextra \
  Lec-11/assignment.cpp/spsc_queue.cpp -o spsc_queue

# Run verification sanity test
./spsc_queue --test

# Run full 1-second benchmark
./spsc_queue
```
*(Can also be compiled with `g++` using the same flags).*
