# Low-Latency Order Book (Capstone Project)

| Student Name | SST Roll No | SST Email |
| :--- | :--- | :--- |
| **Sourabh Srivastva** | 24BCS10157 | sourabh.24bcs10157@sst.scaler.com |

---

## 1. Project Overview

A high-performance C++ resting order book for a single financial instrument, adhering to the Scaler HFT Capstone requirements:
- **Single Instrument**: Single-asset order book with no multiple symbols.
- **Fixed Tick Size**: Tick size of `0.01` with prices strictly in integer multiples of `0.01`.
- **Price Boundaries**: Bids span `[50.00, 99.99]`, asks span `[100.01, 150.00]`, mid-price `100.00` (10,000 discrete price levels).
- **Resting Semantics**: No order matching or trade generation; orders rest until explicitly cancelled.
- **Three Distinct Implementations**: Evaluates three fundamentally different algorithmic and container architectures under identical workloads.
- **Standard Library Only**: Single-threaded implementation using pure ISO C++ (C++17 or later; built with C++20).

---

## 2. Three Implementation Designs

| Implementation | Architecture & Data Structures | Best-Price Strategy | Cancellation Strategy |
| :--- | :--- | :--- | :--- |
| **Implementation A** | Red-Black Trees (`std::map<long, long>`) for bids/asks + `std::unordered_map` for order IDs | $O(1)$ dereference of `m_bids.begin()` and `m_asks.begin()` | $O(1)$ hash map lookup + $O(\log P)$ tree find and node erase |
| **Implementation B** | Dual Priority Queues (`std::priority_queue`) over price-level pointers + `std::unordered_map` for ID and price lookups | Amortized $O(1)$ read of `m_bids.top()`; lazy pruning of stale zero-quantity levels | $O(1)$ hash map lookup + $O(1)$ in-place quantity subtraction (no immediate heap restructuring) |
| **Implementation C** | Sorted Doubly-Linked Lists (`std::list<Order*>`) for bids/asks + `std::unordered_map` for order ID iterators | $O(M_{best})$ forward scan along list head to sum orders at top price | $O(1)$ hash map lookup + $O(1)$ direct iterator unlinking (`m_bids.erase(it)`) |

For comprehensive architecture diagrams, data structure layouts, and complete Big-O analysis, see [implementations.md](implementations.md).

---

## 3. Interface Adaptations & Documentation

All implementations inherit from the abstract interface `IOrderBook` defined in [`order.h`](order.h):

```cpp
enum class Side { Buy, Sell };

struct Level {
    double price{0.0};
    long qty{0};
};

struct BBO {
    Level bid_level{};
    Level ask_level{};
    bool has_bid{false};
    bool has_ask{false};
};

class IOrderBook {
public:
    virtual ~IOrderBook() = default;
    virtual void add_order(uint64_t id, Side side, double price, long qty) = 0;
    virtual bool cancel_order(uint64_t id) = 0;
    virtual BBO get_bbo() const = 0;
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};
```

### Documented Interface Deviations:
1. **Bundled BBO Levels**: Rather than returning separate raw scalars (`double bid_price`, `long bid_qty`), `BBO` embeds the standard `Level` struct (`bid_level`, `ask_level`). `has_bid` and `has_ask` explicitly signal presence, returning `false` on an empty book as required.
2. **Fixed-Point Tick Arithmetic**: While the public interface accepts prices as `double`, prices are immediately normalized to integer ticks internally via `price_to_ticks(price)` ($50.00 \to 5000$). This eliminates IEEE 754 floating-point comparison inaccuracies during map/heap/list operations.
3. **Graceful Truncation**: When requested depth exceeds available active levels, all implementations return all available levels without padding dummy records.
4. **Unknown Order Cancellation**: Cancelling an unknown or already-cancelled order safely returns `false` without modifying state or raising exceptions.

---

## 4. Build Instructions

The project requires CMake 3.16+ and a C++20 compliant compiler (`g++` or `clang++`).

Build with full `-O2` optimization and debug symbols (`-g`) for `perf`:

```bash
# Configure Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets with available CPU cores
cmake --build build -j"$(nproc)"
```

This generates the following binaries in `build/`:
- `bench`: Unified benchmark and correctness verification tool.
- `verify`: Standalone correctness checker.
- `OrderBook`: Default executable demo.
- `OrderBookA`, `OrderBookB`, `OrderBookC`: Standalone implementation demos.

---

## 5. Correctness Verification

The verification command validates all five API methods across all three implementations on an identical generated workload, checking BBO, bid/ask levels, and quantities:

```bash
# Cross-implementation validation on 100,000 operations
./build/bench --verify --ops 100000 --seed 42
```

The verification harness checks the exact requested operation count and returns exit code `0` on success and a nonzero code on failure.

![Correctness Verification](screenshots/verification/correctness.png)

---

## 6. Running the Benchmark

Run each implementation on the required 1,000,000-operation workload (5 runs, median reported, seed 42):

```bash
# Implementation A (std::map)
./build/bench --impl A --ops 1000000 --runs 5 --seed 42

# Implementation B (Priority Queue + Lazy Deletion)
./build/bench --impl B --ops 1000000 --runs 5 --seed 42

# Implementation C (Sorted std::list - see note below)
./build/bench --impl C --ops 100000 --runs 5 --seed 42
```

> [!WARNING]
> **Implementation C Scalability Limitation (1,000,000 Operations)**:  
> Because Implementation C performs an $O(N)$ linear traversal across all resting orders on every `add_order`, total execution time scales quadratically as $O(N^2)$. Running 1,000,000 operations requires an estimated **~2.0 hours per run (~10.0 hours for 5 runs)** due to massive L1/L2 cache misses and DRAM stalls (IPC 0.11). It could not complete within practical benchmarking limits and is explicitly reported as **UNAVAILABLE**. An auxiliary 100,000-operation run was measured to characterize its performance. Consequently, the 1,000,000-operation requirement for Implementation C remains a **known architectural limitation and unmet submission requirement**. Detailed measurements and analysis are in [benchmark.md](benchmark.md).

### Median Performance Summary (Measured on Student's Machine)

| Implementation | 1M Workload Status | Total Time | ns / Add | ns / Cancel | ns / BBO | Checksum |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **A (`std::map`)** | **Completed** | 158.09 ms | 125.7 ns | 242.8 ns | 19.4 ns | `4495793605` |
| **B (`priority_queue`)** | **Completed** | **117.30 ms** | **74.2 ns** | **162.6 ns** | **19.4 ns** | `4495793605` |
| **C (`std::list`, 1M)** | **UNAVAILABLE** | Infeasible (~2h/run) | Unavailable | Unavailable | Unavailable | Unavailable |
| *C (`std::list`, 100k aux)* | *Auxiliary (100k)* | *4,425.58 ms* | *63,040.9 ns* | *177.6 ns* | *61.9 ns* | *404245593* |

---

## 7. Linux `perf` Profiling

### Hardware Counters (`perf stat`)
Hardware counter profiling was executed with 5 repetitions targeting the 9 counters required by `assignment.md`:

```bash
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  -- ./build/bench --impl A --ops 1000000 --runs 1 --seed 42
```
*(Repeat for `--impl B` and `--impl C`).*

| Metric | Implementation A (1M ops) | Implementation B (1M ops) | Implementation C (100k ops Aux) |
| :--- | :---: | :---: | :---: |
| **`IPC` (Instructions / Cycle)** | **0.81** | **0.89** | **0.11** |
| **Branch Miss Rate (%)** | **5.57%** | **2.20%** | **0.13%** |
| **`cache-misses`** | 6,016,766 | 5,477,174 | 726,901,506 |
| **`L1-dcache-load-misses`** | 20,196,215 | 12,749,192 | 993,155,350 |
| **`page-faults`** | 17,433 | 17,476 | 2,670 |

### Hotspot Callgraphs (`perf record` & `perf report`)
Record callchains and inspect hotspots:

```bash
perf record -g -o /var/tmp/perf_a.data -- ./build/bench --impl A --ops 1000000 --runs 1 --seed 42
DEBUGINFOD_URLS="" perf report --stdio -i /var/tmp/perf_a.data --no-children | head -n 25
```

- **Implementation A**: Tree traversal `std::_Rb_tree::_M_lower_bound` and balancing in `add_order` (20.73%) and `cancel_order` (17.27%).
- **Implementation B**: Hash indexing `_Mod_range_hashing` and table rehashing `_M_rehash` in `add_order` (19.47%); lazy deletion eliminates heap operations from cancellation.
- **Implementation C**: Sequential node traversal in `add_order` accounts for **99.05%** of all execution cycles (50.00% bids + 48.95% asks).

For complete profiling tables, counter breakdowns, and answers to Section 11 questions, refer to [benchmark.md](benchmark.md).
