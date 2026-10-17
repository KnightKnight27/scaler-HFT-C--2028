# Low-Latency Order Book — Capstone

**Student:** Shlok Gupta | **SST Roll Number:** 24BCS10322
**Course:** Scaler HFT C++ 2028  
**Language:** C++17

---

## Table of Contents
1. [Project Overview](#project-overview)
2. [How to Build](#how-to-build)
3. [How to Run](#how-to-run)
4. [Interface Changes](#interface-changes)
5. [Data Structure Choices](#data-structure-choices)
6. [Complexity Table](#complexity-table)
7. [Correctness Results](#correctness-results)
8. [Performance Results](#performance-results)
9. [perf Analysis](#perf-analysis)
10. [Performance Report](#performance-report)

---

## Project Overview

This project implements a single-instrument, single-threaded resting order book in three genuinely different ways, benchmarks them against a deterministic 1 000 000-operation workload, and analyses their performance using `std::chrono` and Linux `perf`.

---

## How to Build

### Option 1 — CMake (recommended)

```bash
# From the project root
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# Binary: build/bench
```

### Option 2 — Single g++ command

```bash
g++ -std=c++17 -O2 -march=native -funroll-loops -Wall -Wextra \
    -I include \
    src/bench.cpp \
    -o bench
```

---

## How to Run

### Correctness check

```bash
./bench --verify
```

Expected output:

```
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

### Benchmarks

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

Each run executes the workload **5 times** and reports the **median** result.

### perf profiling (Linux only)

```bash
# perf stat for each implementation
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl B

perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl C

# perf record + report
perf record -g ./bench --impl A && perf report
perf record -g ./bench --impl B && perf report
perf record -g ./bench --impl C && perf report
```

---

## Interface Changes

The public API matches the assignment specification exactly.  
One deliberate internal change:

| Change | Reason |
|--------|--------|
| Prices stored as **integer ticks** internally (`int tick = round(price * 100)`) | Avoids floating-point comparison issues; tick arithmetic is exact and fast |
| `add_order` / `cancel_order` still accept `double price` externally | Fully backward-compatible with the specified API |

All three implementations share the same external interface so the benchmark can test them interchangeably.

---

## Data Structure Choices

### Implementation A — `std::map` (Red-Black Tree)

| Aspect | Detail |
|--------|--------|
| Level storage | `std::map<int, long>` (tick → aggregate qty), one per side |
| Order storage | `std::unordered_map<uint64_t, Order>` |
| Best bid | `bids_.rbegin()` — **O(1)** (std::map maintains begin/end in O(1)) |
| Best ask | `asks_.begin()` — **O(1)** (std::map maintains begin/end in O(1)) |
| Find order for cancel | `orders_.find(id)` — O(1) average |

The red-black tree keeps price levels sorted automatically. Its main cost is pointer-chasing (poor cache locality) and O(log N) insertion/deletion. Iterator access to begin/rbegin is O(1) — the tree keeps a pointer to the min and max nodes.

---

### Implementation B — Sorted `std::vector`

| Aspect | Detail |
|--------|--------|
| Level storage | `std::vector<PriceLevel>` sorted descending (bids) or ascending (asks) |
| Order storage | `std::unordered_map<uint64_t, Order>` |
| Best bid | `bids_.front()` — O(1) |
| Best ask | `asks_.front()` — O(1) |
| Find order for cancel | `orders_.find(id)` O(1) + binary search O(log N) + shift O(N) |

Vectors are contiguous in memory — excellent cache behaviour for reads, but insertion/deletion must shift elements. With ≤ 10 000 levels, this is fast in practice.

---

### Implementation C — Fixed-Size Tick-Indexed Array (Custom Design)

| Aspect | Detail |
|--------|--------|
| Level storage | `std::array<long, 10001>` indexed by `tick - TICK_MIN` |
| Order storage | `std::unordered_map<uint64_t, Order>` |
| Best bid | Cached `best_bid_` cursor — O(1) read |
| Best ask | Cached `best_ask_` cursor — O(1) read |
| Find order for cancel | `orders_.find(id)` O(1) + direct array slot update O(1) + cursor repair O(N) amortised O(1) |

The level array is ~80 KB and fits comfortably in L2 cache. All level updates are a single array write. BBO is always O(1). The cursor repair walk only happens when the best level empties, which is rare in typical markets.

---

## Complexity Table

| Operation | Impl A | Impl B | Impl C |
|-----------|--------|--------|--------|
| `add_order` | O(log N) | O(log N) search + O(N) shift | **O(1)** |
| `cancel_order` | O(log N) | O(log N) search + O(N) shift | **O(1) amort.** |
| `get_bbo` | **O(1)** | **O(1)** | **O(1)** |
| `get_bids(D)` | O(D) | O(D) | O(D) |
| `get_asks(D)` | O(D) | O(D) | O(D) |

N = number of active price levels (max 10 000)  
D = requested depth

---

## Correctness Results

The `--verify` mode runs two test stages:

**Stage 1 — Unit tests (10 groups, 56 assertions)**

| # | Test | Scenario |
|---|------|----------|
| 1 | Empty book | `get_bbo`, `get_bids`, `get_asks` on empty book — no crash, correct flags |
| 2 | Cancel unknown | `cancel_order(999)` never added → `false` |
| 3 | Cancel already-cancelled | cancel twice → first `true`, second `false` |
| 4 | Aggregate qty at same price | two orders at 99.50 → qty sums; cancelling one reduces aggregate |
| 5 | BBO updates — bid | cancel best bid → BBO falls to next level |
| 6 | BBO updates — ask | cancel best ask → BBO rises to next level |
| 7 | Price-band boundaries | orders at 50.00, 99.99, 100.01, 150.00 — all correctly handled |
| 8 | Insufficient depth | 3 levels exist, `get_bids(10)` returns exactly 3 |
| 9 | BBO with multi levels each side | correct best is selected from both sides |
| 10 | Cancel mid-book level | BBO unchanged after non-best level is removed |

**Stage 2 — Differential test: 200 000 ops, seed=42**  
All three implementations replay the same workload and are compared at every BBO query, every depth query, and every cancellation.

```
Running correctness test...

--- Unit tests ---
Unit tests: 56/56 passed

--- Differential test (200 000 ops, seed=42) ---

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
Exit code: 0
```

`./bench --verify` exits with code **0** on success and **1** on any failure (CI-safe).

---

## Performance Results


> **Benchmark environment:** Kali Linux, x86-64; AMD Ryzen AI 9 HX 375 with Radeon 890M; GCC 16.2.0; CMake 4.3.4; Release build.
>
> **Workload:** 1,000,000 base operations, fixed seed 42. Each benchmark implementation is run five times. The executable reports the median run.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Mean total time (ms) | 171.06 | 129.63 | **85.70** |
| Standard deviation (ms) | 28.95 | 11.89 | **9.00** |
| Mean ADD latency (ns) | 135.13 | 99.02 | **47.27** |
| Mean CANCEL latency (ns) | 249.83 | 199.16 | **162.26** |
| Mean BBO latency (ns) | 21.97 | **16.98** | 17.58 |
| IPC | 0.762 | 0.810 | **1.101** |
| Branch-miss rate (%) | 5.60 | 5.74 | **1.34** |
| L1 data-cache load misses | Not measured | Not measured | Not measured |
| Cache misses | 18,554,642 | 12,326,100 | **12,086,599** |
| Page faults | Not measured | Not measured | Not measured |
---

## perf Analysis

The following values were collected using Linux `perf stat -r 5`. IPC and miss rates below are calculated from the recorded counter totals.

### Hardware Counter Results

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| CPU cycles | 4,094,862,698 | 4,072,575,201 | 2,378,156,275 |
| Instructions | 3,122,204,017 | 3,300,682,639 | 2,619,348,944 |
| Instructions per cycle (IPC) | 0.762 | 0.810 | **1.101** |
| Branches | 697,577,945 | 699,053,414 | 551,795,274 |
| Branch misses | 39,048,019 | 40,140,147 | **7,370,637** |
| Branch-miss rate | 5.60% | 5.74% | **1.34%** |
| Cache references | 111,267,511 | 137,242,667 | 60,106,843 |
| Cache misses | 18,554,642 | 12,326,100 | **12,086,599** |
| Cache-miss/reference ratio | 16.68% | **8.98%** | 20.11% |
| `perf` elapsed time (s) | 0.988 | 0.935 | **0.585** |

### Interpretation

**Instructions per cycle:** C recorded approximately 1.10 IPC, compared with 0.81 for B and 0.76 for A. This is consistent with C's lower measured cycle count, but IPC alone does not explain the cause of the performance difference.

**Branch prediction:** C recorded the lowest branch-miss rate (1.34%), compared with 5.60% for A and 5.74% for B. This indicates fewer branch misses in this run; additional profiling would be needed to identify the specific branches responsible.

**Cache behavior:** C had fewer absolute cache misses than A and B, but its cache-miss/reference ratio was highest at 20.11%. The generic `cache-references` and `cache-misses` events are processor-dependent and are not exact L1/L2 data-cache miss rates.

**Measurement scope:** The `perf stat` elapsed time covers the complete executable, including startup, workload generation, and benchmark repetitions. It is not directly comparable to the benchmark's single-run total time.

**Limitations:** The supplied measurements do not include specific L1 data-cache miss, DTLB miss, page-fault, or call-stack profile results. Do not infer or invent those values.

---

## Performance Report

### 1. Which Implementation Is Fastest?

Implementation C achieved the best overall results in the recorded benchmark.

| Comparison | Result |
|---|---|
| C vs. A | 2.00× faster by mean total execution time |
| C vs. B | 1.51× faster by mean total execution time |
| ADD latency | C is fastest at 47.27 ns |
| CANCEL latency | C is fastest at 162.26 ns |
| BBO latency | B is fastest at 16.98 ns |

The results are consistent with the benefits of a fixed-size tick-indexed array: price-level updates use direct indexing rather than tree traversal or sorted-vector element shifting. The cached best-bid and best-ask cursors also make ordinary BBO reads constant-time.

The advantage is workload-dependent. Cursor repair can require scanning price levels when the best level becomes empty, and this benchmark does not represent every possible market workload.

#### 2. Branch Prediction

Implementation C recorded a 1.34% branch-miss rate, substantially below A's 5.60% and B's 5.74%. This may reflect differences in control flow and data-structure traversal, but the counter results alone do not identify individual branch sites responsible for the difference.

### 3. Cache Behavior

| Implementation | Memory access pattern |
|---|---|
| A (`std::map`) | Tree traversal can follow separately allocated nodes, potentially reducing locality |
| B (sorted vector) | Contiguous storage; reads can benefit from spatial locality, while insertions and removals shift elements |
| C (fixed array) | Contiguous tick-indexed level storage with direct indexing |

C recorded 12.09 million cache misses, compared with 18.55 million for A and 12.33 million for B. However, C's cache-miss/reference ratio was the highest at 20.11%. Therefore, the measured data does **not** support claiming that C had the lowest cache-miss rate. Cache-level-specific profiling would be needed to draw stronger conclusions.

### 4. Page Faults

No page-fault counter values were supplied in the recorded results. Therefore, this report makes no measured comparison of page faults between implementations. The fixed level array avoids per-level tree-node allocations, but all implementations still use an `unordered_map` for order storage.

### 5. Production Choice

**Preferred implementation for this workload: Implementation C.**

| Factor | Reasoning |
|---|---|
| Measured latency | Lowest mean total, ADD, and CANCEL latency in the supplied benchmark |
| BBO reads | Constant-time cached best-bid and best-ask access |
| Storage | Fixed-size price-level array avoids tree-node allocations for price levels |
| Predictability | Direct indexing avoids tree traversal and sorted-vector shifting on ordinary updates |
| Trade-offs | Requires a bounded tick range and correct best-price cursor repair |

Implementation B remains competitive for BBO reads and offers contiguous storage. Implementation A provides a conventional ordered-map design with dynamic price-level storage. The measured ranking should be revisited for different instruments, price distributions, and cancellation patterns.


### 6. What Would You Try Next?

1. Calibrate timing overhead and report latency percentiles such as p50, p95, p99, and p99.9.
2. Benchmark different order distributions, price ranges, and cancellation patterns.
3. Capture hardware-specific L1/L2 cache and TLB events where supported.
4. Use `perf record` and call-stack profiling to identify hot functions.
5. Investigate preallocated order storage and object pools to reduce allocation overhead.
6. Evaluate active-price indexing for Implementation C to reduce depth-query scans across empty ticks.
7. Repeat measurements with controlled CPU frequency and reduced background activity.
8. Evaluate multi-threaded extensions separately, with explicit synchronization and correctness tests.

---
