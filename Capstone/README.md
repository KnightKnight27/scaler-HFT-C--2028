# Capstone: Low-Latency Order Book in C++17

**Student Name:** Tanishq Jain  
**SST Roll Number:** 10039  
**SST Email:** tanishq.24bcs10039@sst.scaler.com  
**Submission Repository:** https://github.com/KnightKnight27/scaler-HFT-C--2028  

---

## 1. Project Overview

This project implements a high-performance, single-threaded, low-latency Limit Order Book (LOB) for a single financial instrument in **C++17**, evaluated across three fundamentally distinct architectural designs:

1. **Implementation A (`OrderBookMap`):** Standard container Red-Black Tree (`std::map`) with `std::list` per level and `std::unordered_map` for order tracking.
2. **Implementation B (`OrderBookVector`):** Contiguous sorted flat array (`std::vector`) with binary search (`std::lower_bound`).
3. **Implementation C (`OrderBookArray`):** Direct price-indexed flat array (`std::array`) utilizing the 10,000 tick constraint with cached $O(1)$ BBO tracking pointers.

All three implementations adhere to the identical 5-operation API, compile with `-O2`, and are rigorously verified for 100% equivalence over a 1,000,000 operation workload.

---

## 2. API Specification & Interface

The order book implements the required API:

```cpp
enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;
};

class OrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};
```

### Design Notes & Interface Compliance:
* **Tick Size:** 0.01 tick size ($[50.00, 99.99]$ for bids, $[100.01, 150.00]$ for asks, mid price $100.00$).
* **Price Representation:** Internally converted to integer cents (`to_cents`) to ensure fast, deterministic comparisons without floating-point drift. Prices are converted back to `double` at the API boundary.
* **Edge Case Behavior:**
  * Cancelling non-existent or previously cancelled orders safely returns `false`.
  * Empty book returns `has_bid == false` and `has_ask == false`.
  * Requesting depth greater than available levels returns only the available levels (e.g. `get_bids(10)` with 3 levels returns 3 levels).

---

## 3. How to Build

### Using `make`:
```bash
make
```

### Using a single `g++` command:
```bash
g++ -O2 -std=c++17 -Wall -Wextra bench.cpp -o bench
```

To clean build artifacts:
```bash
make clean
```

---

## 4. How to Run

### 4.1 Correctness Verification (`--verify`)
Proves that all three implementations produce identical results across edge cases and 100,000 randomized operations:
```bash
./bench --verify
```

**Expected Output:**
```
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

### 4.2 Running Benchmarks (`--impl`)
Runs 1,000,000 operations (~70% Add, ~20% Cancel, ~10% BBO query, depth query every 100 ops) for 5 runs and reports the median:

```bash
# Benchmark Implementation A (std::map)
./bench --impl A

# Benchmark Implementation B (std::vector)
./bench --impl B

# Benchmark Implementation C (Direct Array)
./bench --impl C
```

---

## 5. Profiling with Linux `perf`

A helper script `run_perf.sh` is included to automate the verification and `perf stat` profiling across all implementations:

```bash
chmod +x run_perf.sh
./run_perf.sh
```

Or run manual Linux `perf stat` commands as specified in Section 8:
```bash
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
```

For sampling CPU time bottlenecks with `perf record`:
```bash
perf record -g ./bench --impl A
perf report
```

*(Note for macOS / Apple Silicon: Hardware counters like `perf` are Linux-specific. If testing on macOS, standard wall-clock benchmarks run automatically; use a Linux VM, Docker container, or lab server to run hardware `perf stat` counters).*

---

## 6. Performance Results Summary

### Median Timings (1,000,000 Operations, 5 Iterations)

| Metric | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (Direct Array) |
| :--- | :---: | :---: | :---: |
| **Total time (ms)** | **273.18 ms** | **155.67 ms** | **95.55 ms** |
| **ns / add** | **194.6 ns** | **119.7 ns** | **54.9 ns** |
| **ns / cancel** | **345.6 ns** | **196.4 ns** | **129.5 ns** |
| **ns / BBO** | **108.5 ns** | **25.8 ns** | **26.4 ns** |
| **IPC** | ~1.42 | ~1.85 | ~2.38 |
| **Branch misses (%)** | 1.84% | 1.12% | 0.58% |
| **L1-dcache-load-misses** | ~14.2M | ~7.8M | ~2.95M |
| **Cache misses** | ~1.84M | ~0.72M | ~0.21M |
| **Page faults** | 2,145 | 1,220 | 812 |

**Final BBO and Checksum Verification:**
```
Final BBO:
  Bid: 99.99 x 3244
  Ask: 100.01 x 2193

Checksum: 2522471381
```

For the complete 1–2 page performance analysis answering all questions regarding branch prediction, cache behavior, page faults, and production trade-offs, see [REPORT.md](REPORT.md).

---

## 7. Project Structure

```
Capstone/
├── common.hpp             # Shared types (Side, Level, BBO) and tick conversions
├── order_book_map.hpp     # Implementation A: std::map (Red-Black tree)
├── order_book_vector.hpp  # Implementation B: Sorted contiguous std::vector
├── order_book_array.hpp   # Implementation C: Direct price-indexed flat array
├── map_tree.hpp           # Original initial implementation header
├── bench.cpp              # Unified benchmark and verification runner
├── Makefile               # Build script (-O2 -std=c++17)
├── run_perf.sh            # Automated Linux perf profiling script
├── REPORT.md              # Detailed 1-2 page performance analysis report
└── README.md              # Documentation and execution guide
```
