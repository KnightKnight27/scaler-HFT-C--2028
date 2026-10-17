# SPSC Queue 64-Byte Object Benchmark & Throughput Specifications

### Student Information
- **Name:** BISHWAYAN CHATTERJEE
- **Roll:** 24bcs10200
- **Email:** bishwayan.24bcs10200@sst.scaler.com

---

## 1. Overview & Objective

This benchmark evaluates the throughput and latency characteristics of Single-Producer Single-Consumer (SPSC) ring buffer queues processing **64-byte order objects** (`OrderPacket`) within a strict **1-second execution window**.

The assignment investigates:
1. **`std::mutex`**: OS/futex assisted locking on push and pop.
2. **`Spinlock`**: Atomic test-and-set busy-wait loop using `std::atomic_flag` and `_mm_pause()`.
3. **`Lock-Free`**: Cache-line aligned atomic ring buffer using acquire-release memory order semantics as a baseline.

---

## 2. Data Structure: 64-Byte Order Packet

In ultra-low latency exchange gateways and order routing engines, packet structures are aligned to 64 bytes to eliminate false sharing and ensure each order occupies exactly one CPU cache line:

```cpp
struct alignas(64) OrderPacket {
    std::uint64_t orderId;       // 8 bytes
    std::uint64_t timestampNs;   // 8 bytes
    std::uint64_t price;         // 8 bytes
    std::uint32_t quantity;      // 4 bytes
    std::uint32_t traderId;      // 4 bytes
    char client[16];             // 16 bytes (Identified: "Bish")
    std::uint8_t side;           // 1 byte
    std::uint8_t flags;          // 1 byte
    std::uint8_t padding[14];     // 14 bytes
};
static_assert(sizeof(OrderPacket) == 64, "OrderPacket must be exactly 64 bytes");
```

---

## 3. Measured Benchmark Results (1-Second Run)

### Test Configuration
- **Ring Buffer Capacity:** 65,536 slots (power of 2, 4 MB total memory footprint)
- **Object Size:** 64 bytes
- **Producer Thread (`t1`):** Continuously pushes generated `OrderPacket` instances until the 1.000s mark.
- **Consumer Thread (`t2`):** Continuously pops and drains `OrderPacket` instances.
- **Synchronization Verification:** Strict `assert(pushCount == popCount)` verification ensuring 0% packet loss.
- **Run Duration:** 1.000 seconds per synchronization mechanism.

### Hardware Performance Summary

| Synchronization Mode | Total Ops / 1s | Throughput (Mops/s) | Bandwidth (MB/s) | Avg Latency (ns) |
|---|---|---|---|---|
| **`Lock-Free (acquire/rel)`** | **21,092,285** | **20.86 Mops/s** | **1,272.90 MB/s** | **47.95 ns** |
| **`Spinlock (atomic_flag)`** | **17,904,896** | **17.65 Mops/s** | **1,077.27 MB/s** | **56.66 ns** |
| **`std::mutex`** | **17,335,187** | **17.26 Mops/s** | **1,053.29 MB/s** | **57.95 ns** |

---

## 4. Architectural Analysis & Findings

1. **Lock-Free Peak Performance (21.09 Mops/s, 47.95 ns):**
   - The lock-free implementation decouples the write index (`tail_`) and read index (`head_`) onto separate 64-byte cache lines via `alignas(64)`.
   - Producer writes exclusively to its own cache line using `memory_order_relaxed` and synchronizes state via `memory_order_release`.
   - Consumer reads via `memory_order_acquire`, minimizing core-to-core cache line bouncing (MESI invalidations).

2. **Lock-Based Contention Behavior:**
   - Both `std::mutex` and `Spinlock` protect the critical section wrapping `buffer_`, `head_`, and `tail_`.
   - While `Spinlock` achieves 17.90 Mops/s with `_mm_pause()`, both lock variants incur a ~15–18% throughput penalty relative to lock-free due to lock acquisition serialisation and shared cache-line invalidation.

3. **Zero Dynamic Allocation:**
   - The 65,536 slot ring buffer is pre-allocated on initialization. Zero dynamic allocations occur during the 1-second benchmark loop, satisfying the zero-allocation hot-path requirement.

---

## 5. Build & Execution Instructions

### Compilation
Compile with native CPU optimization, static linkage, and strict warning enforcement:
```powershell
g++ -Wall -Wextra -Wpedantic -Werror -O3 -march=native -std=c++23 -static -pthread spsc_queue.cpp -o bench_spsc.exe
```

### Execution
Run the benchmark directly from the assignment folder:
```powershell
.\bench_spsc.exe
```
