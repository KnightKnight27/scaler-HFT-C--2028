email: "rabari.24bcs10657@sst.scaler.com"
roll_no: "24bcs10657"

# High-Performance SPSC Queue Benchmark (64-byte Objects / 1-Second Window)

Implementation and evaluation of a bounded Single-Producer Single-Consumer (SPSC) queue comparing **SpinLock (while loop)**, **`std::mutex`**, and a **Lock-Free (Acquire-Release Atomics)** baseline for 64-byte message objects over a 1.0-second measurement window.

---

## Student Details

- **Email:** `rabari.24bcs10657@sst.scaler.com`
- **Roll No:** `24bcs10657`

---

## System & Environment Specifications

- **CPU:** Apple M4 (10 cores: 4 Performance cores, 6 Efficiency cores)
- **Architecture:** ARM64 (`aarch64`)
- **OS:** macOS Darwin 25.5.0
- **Compiler:** Apple clang version 21.0.0 (clang-2100.1.1.101)
- **Compilation Flags:** `-std=c++17 -O3 -Wall -Wextra -Wpedantic -pthread`

---

## Benchmark Results (1.0-Second Measurement Window)

| Queue Implementation | Pushed Objects | Popped Objects | Throughput (ops/sec) | Data Bandwidth |
| :--- | :---: | :---: | :---: | :---: |
| **SpinLock (while loop)** | 15,340,064 | 15,274,987 | **~15.20 M ops/s** | **927.66 MB/s (0.91 GB/s)** |
| **`std::mutex`** | 6,217,770 | 6,206,657 | **~6.19 M ops/s** | **377.51 MB/s (0.37 GB/s)** |
| **Lock-Free Atomics** *(Reference)* | 22,624,767 | 22,624,767 | **~22.51 M ops/s** | **1,374.01 MB/s (1.34 GB/s)** |

---

## Architectural & Design Highlights

1. **Exact 64-Byte Message Payload:**
   - Message struct has an 8-byte sequence number, 8-byte timestamp, and 48-byte payload field (`sizeof(Message) == 64`), aligned to 64 bytes (`alignas(64)`).
   - Validated at compile-time with `static_assert(sizeof(Message) == 64)`.

2. **Pre-Allocated Memory Pool (Ring Buffer):**
   - The queue pre-allocates a circular ring buffer with a capacity of `65,536` elements at instantiation.
   - Zero dynamic heap memory allocations (`malloc`/`new`) occur during the hot push/pop path.

3. **Power-of-2 Bitwise Masking:**
   - `Capacity` is restricted to powers of 2 (`static_assert((Capacity & (Capacity - 1)) == 0)`).
   - Costly integer modulo instructions (`% Capacity`) are replaced by instantaneous bitwise AND masking (`index & (Capacity - 1)`).

4. **SpinLock (TTAS with CPU Pause):**
   - Uses Test-and-Test-and-Set (TTAS) with `memory_order_acquire` and `memory_order_relaxed`.
   - Emits architecture-specific pause instructions (`yield` on ARM64 / `_mm_pause()` on x86) during contention to reduce CPU pipeline thrashing and bus snooping traffic.

5. **False Sharing Prevention:**
   - Critical queue components (`head_`, `tail_`, `buffer_`, and the lock primitive) are padded and cacheline-aligned (`alignas(64)`), preventing MESI/MOESI cache line invalidation ping-pong between producer and consumer cores.

6. **Correctness Verification:**
   - Includes full verification testing:
     - Pop on empty queue validation
     - Push on full queue boundary validation
     - Concurrent transfer of 1,000,000 items verifying FIFO ordering and data payload integrity

---

## Performance Analysis & Observations

- **SpinLock vs `std::mutex`:**
  - SpinLock achieved **~15.20 M ops/s**, outperforming `std::mutex` (**~6.19 M ops/s**) by **~2.46x**.
  - **Reason:** In an SPSC scenario with ultra-short critical sections (writing a 64-byte struct and incrementing a pointer), `std::mutex` incurs OS futex system calls and kernel scheduler context switches upon contention. The user-space SpinLock stays entirely in user space without thread descheduling overhead.
- **Lock-Free Reference:**
  - Lock-free ring buffer achieved **~22.51 M ops/s (1.34 GB/s)** by eliminating synchronization serialization altogether, allowing the producer and consumer to operate fully independently until queue full/empty boundaries.

---

## How to Build and Run

### 1. Build
```bash
clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -pthread Lec-11/assignment.cpp/spsc_queue.cpp -o spsc_queue
```

### 2. Run
```bash
./spsc_queue
```
