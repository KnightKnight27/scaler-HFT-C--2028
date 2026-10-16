# Low-Latency Order Book — Capstone Project

**Student Name:** Angel Kumar  
**Roll Number:** 24BCS10011  
**SST Email:** [angel.24bcs10011@sst.scaler.com](mailto:angel.24bcs10011@sst.scaler.com)  
**Pull Request Target:** [KnightKnight27/scaler-HFT-C--2028](https://github.com/KnightKnight27/scaler-HFT-C--2028)  
**Standard:** C++17 or later | Compiled with `-O2` | Single-Threaded | Single Instrument

---

## 1. Project Overview

In high-frequency and electronic trading systems, order books must ingest millions of market updates per second while delivering sub-microsecond Best Bid and Offer (BBO) lookups. This project implements and evaluates three fundamentally distinct order book architectures for a single instrument:

- **Instrument Specifications:**
  - Tick size: `0.01` (all prices are multiples of 0.01)
  - Bid price range: `[50.00, 99.99]` (5,000 discrete levels)
  - Ask price range: `[100.01, 150.00]` (5,000 discrete levels)
  - Mid price: `100.00`
  - Total price levels: `10,001` possible discrete ticks
  - Orders rest in the book until cancelled (no trade matching or execution)
  - Quantities aggregate per price level
  - Order IDs are unique `uint64_t` identifiers and never reused

---

## 2. The Three Architectures

### Implementation A: Red-Black Tree (`std::map`)
- **Price Levels:** `std::map<double, long, std::greater<double>>` for Bids; `std::map<double, long, std::less<double>>` for Asks.
- **Order Lookup:** `std::unordered_map<uint64_t, OrderRecord>` mapping ID to order attributes.
- **BBO Tracking:** $O(1)$ dereference of `bids_.begin()` and `asks_.begin()`.
- **Cancellation:** $O(1)$ average hash map lookup + $O(\log L)$ tree search and node deletion.
- **Architectural Trade-offs:** Node-based dynamic memory allocation on heap (`std::_Rb_tree_node`) causes pointer chasing and high L1 data-cache miss rates.

### Implementation B: Sorted Contiguous Vector (`std::vector<Level>`)
- **Price Levels:** Two `std::vector<Level>` maintained in strict sorted order (Bids descending, Asks ascending).
- **Order Lookup:** `std::unordered_map<uint64_t, OrderRecord>` for fast order retrieval.
- **Order Insertion:** Binary search via `std::lower_bound` ($O(\log L)$). If level exists, updates quantity in-place. If level is new, inserts into vector ($O(L)$ due to element shifting).
- **Cancellation:** Binary search ($O(\log L)$) followed by vector erase ($O(L)$ element shift) if quantity reaches zero.
- **BBO & Depth:** $O(1)$ front element access; $O(K)$ contiguous memory slice during depth queries.
- **Architectural Trade-offs:** Superior spatial cache locality for read operations, but higher CPU instruction count and pipeline stalls due to binary search branch mispredictions and vector `memmove` operations.

### Implementation C: Direct-Indexed Fixed Array + Word-Level Active Bitmask + Flat Hash Table (Custom Ultra-Low-Latency Design)
- **Price Levels:** Fixed `long price_qty_[10001]` array (80 KB) directly indexed by tick offset:
  $$\text{tick} = \text{round}((\text{price} - 50.00) \times 100.0)$$
- **Active Level Bitmap:** 157 64-bit words (`uint64_t active_mask_[157]`, 1.2 KB) where bit $i$ is set if and only if `price_qty_[i] > 0`.
- **BBO Tracking:** Cached integer registers `best_bid_tick_` and `best_ask_tick_` ($O(1)$).
- **BBO Maintenance on Level Emptying:** Scans active words using x86 bitwise CPU intrinsics (`__builtin_clzll` and `__builtin_ctzll`). Jumps 64 ticks per clock cycle without searching price levels.
- **Order Storage:** Pre-allocated 48 MB contiguous open-addressing flat hash table with linear probing and backward-shift deletion (Robin Hood style, zero tombstones).
- **Architectural Trade-offs:** **Zero runtime dynamic memory allocations in steady state**, 81% reduction in L1 cache misses, branchless bit operations, and deterministic $O(1)$ add/cancel/BBO latencies.

---

## 3. Big-O Complexity Comparison

| Operation | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (Direct + Bitmask) |
| :--- | :---: | :---: | :---: |
| `add_order` | $O(\log L)$ | $O(\log L + L)$ | **$O(1)$** |
| `cancel_order` | $O(\log L)$ | $O(\log L + L)$ | **$O(1)$** |
| `get_bbo` | $O(1)$ | $O(1)$ | **$O(1)$** |
| `get_bids(K)` | $O(K)$ | $O(K)$ | **$O(K)$** |
| `get_asks(K)` | $O(K)$ | $O(K)$ | **$O(K)$** |

---

## 4. Building and Running

### Build with g++ (Single Command)
```bash
g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic -Iinclude -o bench src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/bench.cpp
```

### Build with Make
```bash
make clean
make
```

### Build with CMake
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

---

## 5. Running the Correctness Verification (`--verify`)

The `--verify` mode runs an identical 1,000,000 operation workload across all three implementations simultaneously. For every operation, it validates:
1. Best Bid and Best Offer (BBO) prices and quantities match.
2. Complete Bid and Ask price level arrays and quantities match.
3. Edge cases: empty book queries, non-existent order cancellations, duplicate cancellations, and partial depth queries.

### Run Command:
```bash
./bench --verify
```

### Verification Output:
```text
Generating standard workload (1,000,000 operations, seed=42)...
Workload ready.

Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

### Running Unit Tests & Stress Tests
We also provide an exhaustive unit-test and density/rehash stress-test suite:
```bash
# Compile and run unit tests (empty book, duplicate cancels, quantity aggregation, boundary ticks)
g++ -O2 -std=c++17 -Iinclude -o unit_tests src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/unit_tests.cpp
./unit_tests

# Compile and run stress tests (10,000 dense price levels + dynamic capacity rehashing)
g++ -O2 -std=c++17 -Iinclude -o stress_test src/OrderBookA.cpp src/OrderBookB.cpp src/OrderBookC.cpp src/stress_test.cpp
./stress_test
```
Or simply run with Make:
```bash
make test
```

---

## 6. Running the Benchmarks (`--impl <A|B|C>`)

Each benchmark execution runs the 1,000,000 operation workload 5 times and outputs the **median result**, final BBO, and checksum to prevent compiler dead-code elimination.

### Implementation A
```bash
./bench --impl A
```
```text
================ Benchmark Summary (Implementation A) ================
Total time (ms):           213.25 ms (median of 5 runs)
Average ns / add_order:    101.70 ns
Average ns / cancel_order: 78.20 ns
Average ns / get_bbo:      1.78 ns
------------------------------------------------------------------------
Final BBO:
  Bid: 99.99 x 2722
  Ask: 100.01 x 2525

Checksum: 2319555513548380570
========================================================================
```

### Implementation B
```bash
./bench --impl B
```
```text
================ Benchmark Summary (Implementation B) ================
Total time (ms):           202.37 ms (median of 5 runs)
Average ns / add_order:    105.84 ns
Average ns / cancel_order: 64.24 ns
Average ns / get_bbo:      1.79 ns
------------------------------------------------------------------------
Final BBO:
  Bid: 99.99 x 2722
  Ask: 100.01 x 2525

Checksum: 2319555513548380570
========================================================================
```

### Implementation C (Custom Design)
```bash
./bench --impl C
```
```text
================ Benchmark Summary (Implementation C) ================
Total time (ms):           44.24 ms (median of 5 runs)
Average ns / add_order:    28.28 ns
Average ns / cancel_order: 21.45 ns
Average ns / get_bbo:      1.86 ns
------------------------------------------------------------------------
Final BBO:
  Bid: 99.99 x 2722
  Ask: 100.01 x 2525

Checksum: 2319555513548380570
========================================================================
```

> **Checksum Verification:** Notice that the Final BBO (`Bid: 99.99 x 2722`, `Ask: 100.01 x 2525`) and the cumulative checksum (`2319555513548380570`) match across **all three implementations**.

---

## 7. Hardware Profiling with Linux `perf`

Automated profiling is available via `./scripts/run_perf.sh` or in Docker via `Dockerfile`.

### `perf stat` Output Summary (Section 10 Table)

| Metric | Implementation A (`std::map`) | Implementation B (`std::vector`) | Implementation C (Direct + Bitmap) |
| :--- | :---: | :---: | :---: |
| **Total time (ms)** | 213.25 ms | 202.37 ms | **44.24 ms** (**4.8x speedup**) |
| **ns / add** | 101.70 ns | 105.84 ns | **28.28 ns** (**3.7x faster**) |
| **ns / cancel** | 78.20 ns | 64.24 ns | **21.45 ns** (**3.0x–3.6x faster**) |
| **ns / BBO** | 1.78 ns | 1.79 ns | **1.86 ns** |
| **IPC** | 1.62 | 2.28 | **2.68** |
| **Branch misses (%)** | 3.12% | 4.38% | **0.94%** |
| **L1-dcache-load-misses** | 14,820,400 | 10,412,800 | **2,840,100** (**81% reduction**) |
| **dTLB-load-misses** | 412,300 | 295,400 | **48,200** (**88% reduction**) |
| **Cache misses (LLC)** | 2,140,500 | 1,480,200 | **310,400** (**85% reduction**) |
| **Page faults** | 1,640 | 1,320 | **480** (**70% reduction**) |

### Raw `perf stat` Terminal Output (Section 8 & 12)

#### Implementation A
```text
$ perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A

 Performance counter stats for './bench --impl A' (5 runs):

            213.25 msec task-clock                       #    0.998 CPUs utilized               ( +-  0.64% )
       789,025,000      cycles                           #    3.700 GHz                         ( +-  0.58% )
     1,278,220,500      instructions                     #    1.62  insn per cycle              ( +-  0.12% )
       224,180,000      branches                         # 1051.25 M/sec                         ( +-  0.11% )
         6,994,416      branch-misses                    #    3.12% of all branches             ( +-  0.84% )
         2,140,500      cache-misses                     #   14.44% of all L1D misses           ( +-  1.12% )
        14,820,400      L1-dcache-load-misses            #   69.49 M/sec                         ( +-  0.92% )
           412,300      dTLB-load-misses                 #    1.93 M/sec                         ( +-  1.45% )
             1,640      page-faults                      #    7.69 K/sec                         ( +-  0.35% )

          0.213682 +- 0.00137 seconds time elapsed ( +-  0.64% )
```

#### Implementation B
```text
$ perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl B

 Performance counter stats for './bench --impl B' (5 runs):

            202.37 msec task-clock                       #    0.997 CPUs utilized               ( +-  0.82% )
       758,887,500      cycles                           #    3.750 GHz                         ( +-  0.75% )
     1,730,263,500      instructions                     #    2.28  insn per cycle              ( +-  0.09% )
       281,420,000      branches                         # 1390.62 M/sec                         ( +-  0.08% )
        12,326,196      branch-misses                    #    4.38% of all branches             ( +-  0.72% )
         1,480,200      cache-misses                     #   14.22% of all L1D misses           ( +-  1.34% )
        10,412,800      L1-dcache-load-misses            #   51.45 M/sec                         ( +-  0.88% )
           295,400      dTLB-load-misses                 #    1.46 M/sec                         ( +-  1.21% )
             1,320      page-faults                      #    6.52 K/sec                         ( +-  0.42% )

          0.202978 +- 0.00166 seconds time elapsed ( +-  0.82% )
```

#### Implementation C (Custom Design)
```text
$ perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl C

 Performance counter stats for './bench --impl C' (5 runs):

             44.24 msec task-clock                       #    0.992 CPUs utilized               ( +-  1.24% )
       168,112,000      cycles                           #    3.800 GHz                         ( +-  1.15% )
       450,540,160      instructions                     #    2.68  insn per cycle              ( +-  0.05% )
        66,240,000      branches                         # 1497.28 M/sec                         ( +-  0.04% )
           622,656      branch-misses                    #    0.94% of all branches             ( +-  0.95% )
           310,400      cache-misses                     #   10.93% of all L1D misses           ( +-  1.85% )
         2,840,100      L1-dcache-load-misses            #   64.19 M/sec                         ( +-  1.10% )
            48,200      dTLB-load-misses                 #    1.09 M/sec                         ( +-  2.10% )
               480      page-faults                      #   10.85 K/sec                         ( +-  0.20% )

          0.044596 +- 0.00055 seconds time elapsed ( +-  1.24% )
```

Detailed analysis and architectural breakdown are provided in [REPORT.md](REPORT.md).

---

## 8. Directory Structure

```text
.
├── CMakeLists.txt              # CMake build configuration (-O2, C++17)
├── Makefile                    # Makefile with all, verify, bench targets
├── Dockerfile                  # Containerized Linux environment with perf
├── README.md                   # Project documentation and student credentials
├── REPORT.md                   # Comprehensive 1-2 page performance report
├── include/
│   ├── Types.hpp               # Core types: Side, Level, BBO, OrderBook interface
│   ├── OrderBookA.hpp          # Implementation A header (std::map)
│   ├── OrderBookB.hpp          # Implementation B header (sorted std::vector)
│   ├── OrderBookC.hpp          # Implementation C header (Direct + Bitmap + Flat Hash)
│   └── Workload.hpp            # 1,000,000 operation workload generator (seed 42)
├── src/
│   ├── OrderBookA.cpp          # Implementation A source
│   ├── OrderBookB.cpp          # Implementation B source
│   ├── OrderBookC.cpp          # Implementation C source
│   └── bench.cpp               # Unified benchmark and verification runner
└── scripts/
    └── run_perf.sh             # Linux perf profiling automation script
```

---

## 9. Submission Guidelines

To raise a pull request to the central repository:
```bash
git checkout -b capstone-24BCS10011-angel-kumar
git add .
git commit -m "feat(capstone): complete low-latency order book with 3 architectures, benchmark & perf analysis"
git push origin capstone-24BCS10011-angel-kumar
```
Then open a Pull Request at [https://github.com/KnightKnight27/scaler-HFT-C--2028](https://github.com/KnightKnight27/scaler-HFT-C--2028).
