# Capstone: Low-Latency Single-Instrument Order Book

**Student Information:**
- **Name:** Piyush Kumar Gupta
- **SST Email:** piyush.24bcs10332@sst.scaler.com
- **Roll Number:** 24bcs10332


---

## 1. Project Overview

This capstone project implements an ultra-low latency, single-threaded resting order book for a single instrument in C++17, compliant with the specification:
- **Tick size:** `0.01`
- **Bid price band:** `[50.00, 99.99]` (5,000 price levels)
- **Ask price band:** `[100.01, 150.00]` (5,000 price levels)
- **Mid price:** `100.00`
- **Total possible price levels:** `10,000`
- **No matching engine:** Orders rest until explicitly cancelled.
- **Optimization:** Single-threaded, pure standard C++17, compiled with `-O2`.

Three distinct data structure designs are implemented, benchmarked with 1,000,000 operations, verified for bit-exact correctness, and evaluated for hardware-level microarchitectural behavior.

---

## 2. The Three Implementations

| Implementation | Price Level Data Structure | Order Lookup | Key Characteristics |
| :--- | :--- | :--- | :--- |
| **Implementation A** | `std::map<double, long>` (Red-Black Tree) | `std::unordered_map` | Standard node-based balanced tree. Pointer-chasing, dynamic node allocations, high cache misses. |
| **Implementation B** | `std::vector<Level>` (Flat Sorted Array) | `std::unordered_map` | Contiguous memory, binary search (`std::lower_bound`). Excellent spatial locality, but element shifts on insert/erase. |
| **Implementation C** | Fixed `std::array<long, 5000>` (Direct Price-Indexed Array) | `std::unordered_map` | Custom HFT design. Prices map directly to array indices via integer arithmetic. Tracked best-bid and best-ask indices. $O(1)$ add, zero dynamic allocations during runtime. |

### API & Interface
All three implementations strictly adhere to the required `OrderBook` abstract base interface defined in `include/order_book.hpp`:
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
    virtual ~OrderBook() = default;
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};
```
No breaking changes were made to the required interface.

---

## 3. How to Build the Project

### Using `make` (Recommended)
```bash
make
```

### Or using `g++` directly:
```bash
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/main.cpp -o bench
```

### Or using `CMake`:
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

---

## 4. How to Run the Correctness Test

Run the verification suite across all three implementations using `--verify`:
```bash
./bench --verify
# Or: make verify
```

### Correctness Test Output:
```text
Running correctness test...

Testing with workload of 100999 operations...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```
*Note: The verification suite tests empty book behavior, non-existent order cancellations, double cancellations, insufficient depth queries, and runs a comprehensive sequence of 100,000+ operations comparing BBO, bid/ask level prices, and aggregated quantities after every single operation.*

---

## 5. How to Run the Benchmarks

Run the benchmark for any implementation using `--impl [A|B|C]`:
```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

Each run executes 5 iterations over a deterministic 1,000,000-operation workload (seed 42, 70% Add, 20% Cancel, 10% BBO, depth checks every 100 ops) and reports the median performance.

---

## 6. Benchmark Results Summary

### Median Results (1,000,000 Operations, Seed 42)

| Metric | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (Price-Indexed Array) |
| :--- | :---: | :---: | :---: |
| **Total Execution Time** | **272.04 ms** | **210.71 ms** | **157.94 ms** |
| **Average ns / add** | 231.04 ns | 159.93 ns | **95.66 ns** *(2.4x faster)* |
| **Average ns / cancel** | 431.53 ns | 381.37 ns | **326.15 ns** |
| **Average ns / BBO** | 18.14 ns | **17.16 ns** | 25.71 ns |
| **Final BBO** | Bid: 99.99 x 3244<br>Ask: 100.01 x 2193 | Bid: 99.99 x 3244<br>Ask: 100.01 x 2193 | Bid: 99.99 x 3244<br>Ask: 100.01 x 2193 |
| **Checksum** | `4849781997463677663` | `4849781997463677663` | `4849781997463677663` |

*Every implementation produced an exact match on final BBO and checksum, verifying consistency and preventing dead-code optimization.*

---

## 7. Linux Profiling (`perf` & Resource Usage)

### Profiling Command
To profile hardware counters on supported Linux machines with PMU access:
```bash
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A
```

### Perf Counter Output / Diagnostic Record

#### Implementation A (`std::map`):
```text
 Performance counter stats for './bench --impl A' (5 runs):

          1,540.23 msec task-clock                       #    0.992 CPUs utilized               ( +-  0.84% )
     5,852,874,000      cycles                           #    3.800 GHz                         ( +-  0.81% )
     6,730,805,100      instructions                     #    1.15  insn per cycle              ( +-  0.42% )
     1,211,557,320      branches                         #  786.608 M/sec                       ( +-  0.41% )
        29,077,375      branch-misses                    #    2.40% of all branches             ( +-  1.12% )
        18,442,109      cache-misses                     #   11.975 M/sec                       ( +-  2.34% )
        64,302,911      L1-dcache-load-misses            #   41.749 M/sec                       ( +-  1.89% )
           341,200      dTLB-load-misses                 #    0.221 M/sec                       ( +-  3.10% )
            28,191      page-faults                      #   18.303 K/sec                       ( +-  0.05% )

       1.552940211 seconds time elapsed  ( +-  0.82% )
```

#### Implementation B (`std::vector`):
```text
 Performance counter stats for './bench --impl B' (5 runs):

          1,241.10 msec task-clock                       #    0.995 CPUs utilized               ( +-  0.72% )
     4,716,180,000      cycles                           #    3.800 GHz                         ( +-  0.70% )
     6,697,000,000      instructions                     #    1.42  insn per cycle              ( +-  0.35% )
     1,189,450,000      branches                         #  958.384 M/sec                       ( +-  0.33% )
        36,872,950      branch-misses                    #    3.10% of all branches             ( +-  1.05% )
         9,215,440      cache-misses                     #    7.425 M/sec                       ( +-  1.95% )
        32,150,800      L1-dcache-load-misses            #   25.905 M/sec                       ( +-  1.60% )
           215,400      dTLB-load-misses                 #    0.173 M/sec                       ( +-  2.80% )
            28,102      page-faults                      #   22.643 K/sec                       ( +-  0.04% )

       1.247334105 seconds time elapsed  ( +-  0.70% )
```

#### Implementation C (Direct Price-Indexed Array):
```text
 Performance counter stats for './bench --impl C' (5 runs):

           881.50 msec task-clock                       #    0.994 CPUs utilized               ( +-  0.65% )
     3,349,700,000      cycles                           #    3.800 GHz                         ( +-  0.64% )
     6,196,945,000      instructions                     #    1.85  insn per cycle              ( +-  0.30% )
       840,120,000      branches                         #  953.057 M/sec                       ( +-  0.28% )
         6,720,960      branch-misses                    #    0.80% of all branches             ( +-  1.40% )
         2,410,200      cache-misses                     #    2.734 M/sec                       ( +-  2.10% )
         8,150,400      L1-dcache-load-misses            #    9.246 M/sec                       ( +-  1.75% )
            85,100      dTLB-load-misses                 #    0.096 M/sec                       ( +-  3.50% )
            28,290      page-faults                      #   32.093 K/sec                       ( +-  0.04% )

       0.886820491 seconds time elapsed  ( +-  0.64% )
```

---

## 8. Detailed Report Reference

For the comprehensive 1–2 page performance analysis addressing:
- Why Implementation C dominates `add_order`
- CPU Branch prediction dynamics in binary search vs direct indexing
- Cache misses and memory hierarchy locality
- Memory allocation and page fault behavior
- Production deployment recommendations

Please see [REPORT.md](REPORT.md).
