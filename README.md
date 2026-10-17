# Scaler HFT C++ (2028)

**Student Name:** Tanishq Jain  
**SST Roll Number:** 10039  
**SST Email:** tanishq.24bcs10039@sst.scaler.com  
**Repository:** [https://github.com/tanishq4141/scaler-HFT-C--2028](https://github.com/tanishq4141/scaler-HFT-C--2028)

---

## 🚀 Featured Capstone: Low-Latency Limit Order Book (LOB)

The final capstone project is located in the **[`Capstone/`](./Capstone)** directory.

It implements an ultra-low-latency, single-threaded Limit Order Book in **C++17** evaluated across three distinct architectural paradigms to study the real-world hardware impact of cache locality, branch prediction, and memory hierarchy.

### Architectures Implemented

1. **Implementation A (`OrderBookMap`):**
   * Standard container Red-Black Tree (`std::map`) with node-based `std::list` per level and `std::unordered_map` for $O(1)$ order cancellation lookups.
2. **Implementation B (`OrderBookVector`):**
   * Contiguous flat array (`std::vector`) kept sorted via binary search (`std::lower_bound`). Maximizes spatial locality for level scans and $O(1)$ BBO access.
3. **Implementation C (`OrderBookArray`):**
   * Direct price-indexed flat array (`std::array`) utilizing the 10,000 tick price constraint with cached $O(1)$ BBO tracking pointers and zero heap allocations in the critical path.

---

### Quick Benchmark Comparison (1,000,000 Operations)

| Metric | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (Direct Array) |
| :--- | :---: | :---: | :---: |
| **Total Time (ms)** | **273.18 ms** | **155.67 ms** | **95.55 ms** |
| **ns / add** | 194.6 ns | 119.7 ns | **54.9 ns** |
| **ns / cancel** | 345.6 ns | 196.4 ns | **129.5 ns** |
| **ns / BBO** | 108.5 ns | 25.8 ns | **26.4 ns** |
| **IPC** | ~1.42 | ~1.85 | **~2.38** |
| **Branch Misses** | 1.84% | 1.12% | **0.58%** |
| **L1-dcache Misses** | ~14.2M | ~7.8M | **~2.95M** |
| **LLC Cache Misses** | ~1.84M | ~0.72M | **~0.21M** |

> For comprehensive performance analysis, hardware counter metrics (`perf stat`), cache hierarchy explanations, and production considerations, refer to the full [Capstone Performance Report (REPORT.md)](./Capstone/REPORT.md).

---

### Quickstart: Building & Running the Capstone

```bash
# Navigate to Capstone directory
cd Capstone

# Build with -O2 optimization
make

# 1. Run correctness verification across edge cases & 100k random ops
./bench --verify

# 2. Benchmark individual implementations (1,000,000 ops, 5 iterations)
./bench --impl A   # std::map
./bench --impl B   # std::vector
./bench --impl C   # Direct Array

# 3. (Optional on Linux) Run automated perf profiling
./run_perf.sh
```

For full details, API contracts, and profiling scripts, see the [Capstone Documentation (README.md)](./Capstone/README.md).

---

## 📁 Repository Structure

```
scaler-HFT-C--2028/
├── Capstone/               # Capstone: Low-Latency Limit Order Book (C++17)
│   ├── common.hpp          # Order book types (Side, Level, BBO) & tick conversions
│   ├── order_book_map.hpp  # Implementation A: std::map (Red-Black tree)
│   ├── order_book_vector.hpp # Implementation B: Contiguous std::vector
│   ├── order_book_array.hpp  # Implementation C: Direct price-indexed array
│   ├── bench.cpp           # Unified benchmark & verification harness
│   ├── Makefile            # Build configuration (-O2 -std=c++17)
│   ├── run_perf.sh         # Linux perf profiling script
│   ├── README.md           # Capstone build & run guide
│   └── REPORT.md           # In-depth architectural & performance report
├── Lec-1/ to Lec-12/       # Lecture assignments, low-level C++ experiments,
│                           # memory layouts, and systems programming exercises
└── README.md               # Repository documentation (this file)
```
