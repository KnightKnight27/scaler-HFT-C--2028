# Low-Latency Order Book — Capstone Project

**Name:** Sanjay  
**Roll No:** 24bcs10419  
**Email:** sanjay.24bcs10419@sst.scaler.com  
**Language:** C++17 | **Compiler Flags:** `-O2`

---

## Project Structure

```
├── CMakeLists.txt
├── .gitignore
├── README.md
├── REPORT.md
├── perf_run.sh
├── include/
│   ├── common.h
│   ├── orderbook_a.h
│   ├── orderbook_b.h
│   └── orderbook_c.h
└── src/
    └── bench.cpp
```

---

## Build

Single command:

```bash
g++ -std=c++17 -O2 -o bench src/bench.cpp -I include
```

Using CMake:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

---

## Correctness Verification

To verify that all three implementations produce identical results across the 1,000,000 operation workload:

```bash
./bench --verify
```

Output:

```
Generating workload (1000000 operations, seed=42)...
Workload generated: 1010000 total ops (including depth queries).

===== Correctness Verification =====
  Checked 109675 consistency points.

  Implementation A: PASS
  Implementation B: PASS
  Implementation C: PASS

  All implementations produce identical results.
```

---

## Benchmark

Run individual implementations:

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

Each benchmark executes 5 consecutive runs with a fixed random seed (`42`) and reports median values.

---

## Linux Profiling (`perf`)

Run the automated profiling script:

```bash
chmod +x perf_run.sh
./perf_run.sh
```

Or manually:

```bash
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

perf record -g ./bench --impl A
perf report
```

---

## Internal Price Representation

Prices are stored and indexed internally as integer ticks rather than floating-point numbers (`double`) to prevent precision and rounding errors:

- `tick = static_cast<int>(price * 100.0 + 0.5)`
- `price = tick * 0.01`

Price bands:
- Bids: `[50.00, 99.99]` -> ticks `5000` to `9999`
- Asks: `[100.01, 150.00]` -> ticks `10001` to `15000`
- Total: `10,001` possible tick indices

The public interface accepts `double` and returns `double` for prices, so callers interact directly with the specified API.

---

## Data Structure Designs

### Implementation A: `std::map`

- **Price Levels:** `std::map<int, long, std::greater<int>>` for bids (descending) and `std::map<int, long>` for asks (ascending).
- **Order Lookup:** `std::unordered_map<uint64_t, Order>` mapping order ID to order details.
- **BBO:** `begin()` on each map gives the best price in O(1) time.
- **Add / Cancel:** Node insertion and deletion require O(log P) red-black tree rebalancing.
- **Depth:** Iterating the first D elements of the map takes O(D).

### Implementation B: `std::unordered_map` with Tracked BBO

- **Price Levels:** `std::unordered_map<int, long>` for both bids and asks.
- **Order Lookup:** `std::unordered_map<uint64_t, Order>`.
- **BBO:** Maintained via `best_bid_` and `best_ask_` tick variables.
- **Add:** O(1) amortized hash table insertion and tick comparison.
- **Cancel:** O(1) amortized, but if the cancelled order empties the current best level, a scan across all active keys in the hash table is required to find the next best price.
- **Depth:** Requires dumping all active price keys, sorting them, and picking the top D levels.

### Implementation C: Fixed-Size Tick-Indexed Array

- **Price Levels:** Flat `std::array<long, 10001>` indexed by `tick - 5000` (~80 KB, fits inside L2 cache).
- **Order Lookup:** `std::unordered_map<uint64_t, Order>`.
- **BBO:** Tracked in `best_bid_` and `best_ask_`.
- **Add:** Direct array write `levels_[tick - MIN_TICK] += qty` in O(1) with no dynamic allocation or tree traversal.
- **Cancel:** Decrement quantity at index in O(1). If the best level becomes empty, linear scan down/up across contiguous memory. Because the array is contiguous, the hardware stream prefetcher handles the scan efficiently.
- **Depth:** Linear scan from best tick until D non-empty levels are gathered.

---

## Complexity Comparison

| Operation | Implementation A (`std::map`) | Implementation B (`std::unordered_map`) | Implementation C (Array) |
|---|---|---|---|
| `add_order` | O(log P) | O(1) amortized | O(1) |
| `cancel_order` | O(log P) | O(1) amortized / O(P) worst | O(1) amortized / O(T) worst |
| `get_bbo` | O(1) | O(1) | O(1) |
| `get_bids(D)` | O(D) | O(P log P) | O(D) best / O(T) worst |
| `get_asks(D)` | O(D) | O(P log P) | O(D) best / O(T) worst |

*P = active price levels, D = requested depth (10), T = tick range (5000 ticks per side)*

---

## Results Table

*(Measured on Windows 11 x64, MSVC /O2, 1,000,000 operations, fixed seed 42, median of 5 runs)*

| Metric | Implementation A (`std::map`) | Implementation B (`unordered_map`) | Implementation C (Array) |
|---|---|---|---|
| **Total time (ms)** | 496.92 ms | 10,343.24 ms | **442.09 ms** |
| **ns / add** | 475.7 ns | 584.3 ns | **417.6 ns** |
| **ns / cancel** | 611.4 ns | 826.3 ns | **497.5 ns** |
| **ns / BBO** | 35.7 ns | 81.9 ns | 39.8 ns |
| **Final BBO Checksum** | -2054320158 | -2054320158 | -2054320158 |
| **Correctness (109,675 checks)** | PASS | PASS | PASS |

*(For full performance analysis covering CPU cache lines, branch prediction, page faults, and production evaluation, see [REPORT.md](REPORT.md).)*
