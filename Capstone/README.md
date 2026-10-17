# Low-Latency Order Book in C++ — Three Implementations

**Name:** Mohammed Rehan  
**Email:** mohammed.24bcs10620@sst.scaler.com  
**Roll number:** 10620

A single-instrument, single-threaded resting order book implemented with three different data structures. All three designs run the same deterministic workload so their correctness and performance can be compared fairly.

## Project at a glance

- **Language:** C++17
- **Build:** `g++ -std=c++17 -O2 -Wall -Wextra -pedantic bench.cpp -o bench`
- **Workload:** 1,000,000 operations, fixed seed, same generated operations for A/B/C
- **Correctness:** all three implementations pass edge tests and shared-workload verification
- **Profiling:** `perf stat` captured for A/B/C; `perf record`/`perf report` were not run

## 1. Problem and constraints

- Tick size is `0.01`.
- Bid prices range from `50.00` to `99.99` (ticks `5000`–`9999`).
- Ask prices range from `100.01` to `150.00` (ticks `10001`–`15000`).
- `100.00` is the mid-price; orders are not placed at that price.
- Multiple orders can exist at one price. The price level stores their combined quantity.
- Orders rest until cancelled. There is no matching engine or trade generation.
- The implementation uses integer price ticks internally instead of floating-point prices. This is a documented interface choice that avoids floating-point comparison issues.

## 2. Design overview

```text
                      Shared benchmark workload
                                |
                +---------------+---------------+
                |               |               |
             Impl A          Impl B          Impl C
           unordered_map      std::map      arrays + bitmap
                |               |               |
                +---------------+---------------+
                                |
                    Same logical order-book API
```

Each implementation keeps two kinds of state:

```text
Order ID  -> side, price tick, order quantity
Price tick -> aggregate quantity for that level
```

The order-ID index finds an individual order during cancellation. The price-level structure keeps the aggregate quantity used by BBO and depth queries.

## 3. The three implementations

### A — Hash maps for price levels (`Impl1.cpp`)

```text
bids: price tick -> aggregate quantity
asks: price tick -> aggregate quantity
orders: order ID -> order details
```

- Price-level updates use `std::unordered_map`.
- Best bid and ask are found by scanning active price levels.
- Depth queries collect levels and use `partial_sort` to return the best requested prices.
- **Trade-off:** straightforward updates, but scanning for BBO and sorting depth results costs time; hash-table access is average-case constant time, not a worst-case guarantee.

During verification, Implementation A initially failed repeated-cancellation testing because the cancelled order remained in the order-ID map. The fix was to erase the order entry after using its saved details to update the aggregate level. The malformed `emplace` expression and `return` typo were also corrected.

### B — Ordered price maps (`Impl2.cpp`)

```text
bids: std::map<price tick, aggregate quantity>
asks: std::map<price tick, aggregate quantity>
orders: unordered_map<order ID, order details>
```

- The highest bid is at `bids.rbegin()`; the lowest ask is at `asks.begin()`.
- Price-level insertion, lookup, and removal take `O(log P)`, where `P` is the number of active levels.
- Depth queries traverse the maps in price priority order and stop at the requested depth.
- **Trade-off:** efficient best-price access and naturally sorted levels, but tree nodes involve pointer chasing and dynamic allocation.

### C — Fixed arrays and an occupancy bitmap (`Impl3.cpp`)

```text
Bid ticks 5000..9999   -> array indices 0..4999
Ask ticks 10001..15000 -> array indices 0..4999

quantity[index] = aggregate quantity
bitmap[index]   = whether that price level is active
```

- A price tick maps directly to an array index.
- The bitmap records which levels are occupied.
- BBO searches the bounded bitmap; depth queries scan prices in best-price order.
- **Trade-off:** fixed and contiguous price-level storage, with bounded search over the supported price range. The order-ID index still uses an `unordered_map`.

## 4. Complexity summary

Let `P` be active price levels, `D` the requested depth, and `L = 5000` levels per side. Hash-map operations are average-case.

| Operation | A: hash maps | B: ordered maps | C: arrays + bitmap |
|---|---|---|---|
| Add | Average `O(1)` | `O(log P)` price-level update | Average `O(1)` |
| Cancel | Average `O(1)` | Average ID lookup plus `O(log P)` price update | Average `O(1)` |
| BBO | `O(P)` scan | `O(1)` endpoint access | Bounded bitmap search (79 words per side at most) |
| Bid/ask depth | `O(P log D)` with partial sort | `O(D)` traversal | `O(L)` worst case |

C's search is bounded because the assignment fixes the valid price range; it is not constant-time for an arbitrarily expandable price range.

## 5. Build and correctness verification

Build from the repository root:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pedantic bench.cpp -o bench
```

Run correctness tests:

```bash
./bench --verify
```

The verification checks edge cases and runs the same one-million-operation workload through all implementations. It compares cancellation results, BBO values, top-ten bid/ask levels at scheduled checkpoints, and final state.

**Observed result:** all three implementations passed the edge tests and the shared workload.

```text
Implementation A: edge tests PASS
Implementation B: edge tests PASS
Implementation C: edge tests PASS
Running correctness verification on 1000000 shared operations...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations agree on the shared workload.
```

Evidence: [`screenshots/correctness.png`](screenshots/correctness.png).

## 6. Workload and benchmark method

Run each implementation without `perf` to collect the benchmark's own timings:

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

The benchmark defaults to five runs and reports medians. `--runs 1` is available for a single quick run. The workload has 1,000,000 operations, approximately 70% adds, 20% cancellations, and 10% BBO queries; it uses a fixed seed, a 50/50 buy/sell split, quantities from 1–100, valid prices, unique increasing order IDs, and cancellation IDs selected from live orders. `get_bids(10)` and `get_asks(10)` are called every 100 operations.

The per-operation `steady_clock` measurements include timer-call overhead. Total runtime includes scheduled depth queries but excludes workload generation and final-state formatting. Keep compiler settings and machine conditions consistent when comparing runs.

Screenshots of the benchmark output:

- [Implementation A](screenshots/benchmark-a.png)
- [Implementation B](screenshots/benchmark-b.png)
- [Implementation C](screenshots/benchmark-c.png)

## 7. Hardware-counter measurements (`perf stat`)

Linux initially blocked performance-counter access because `kernel.perf_event_paranoid` was set to `4`. After temporarily adjusting the setting, the following command completed for each implementation:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A --runs 1
```

The command was repeated with `--impl B` and `--impl C`. `-r 5` repeats the single-run benchmark five times; `--runs 1` avoids nesting the benchmark's own five-run loop inside `perf`.

### Observed `perf stat` summary

Values below are from the captured terminal output. Elapsed time is instrumented time under `perf`, not the final uninstrumented benchmark median.

| Metric | A | B | C |
|---|---:|---:|---:|
| Elapsed time | 18.327 s | 3.6993 s | 3.34796 s |
| Cycles | 65.73 billion | 13.26 billion | 12.23 billion |
| Instructions | 19.98 billion | 2.32 billion | 2.22 billion |
| Branch misses | 53.06 million | 58.23 million | 51.85 million |
| Branch-miss rate | 11.66% | 11.73% | 11.04% |
| Cache misses | 1.02 billion | 131.49 million | 61.89 million |
| L1 data-cache load misses | 1.45 billion | 2.62 billion | 1.24 billion |
| dTLB load misses | 19.40 million | 14.69 million | 10.67 million |
| Page faults | 16,351 | 16,349 | 16,210 |

Screenshots: [A stats](screenshots/perf/perf-a-stats.png), [B stats](screenshots/perf/perf-b-stats.png), [C stats](screenshots/perf/perf-c-stats.png). Additional `perf` screenshots are also stored in the same directory: [`perf/`](screenshots/perf/).

### Initial interpretation

- **A has substantially higher elapsed time, cycles, instructions, and cache misses** in this workload. Its BBO implementation scans active price levels, which is a plausible contributor; the measurements alone do not prove that it is the only cause.
- **C has the lowest observed elapsed time and cache-miss count** among these instrumented runs. Its fixed arrays provide contiguous price-level storage and its bitmap bounds the BBO search.
- **B has slightly more branch misses than A and C**, both by count and by rate. A raw branch-miss count should be interpreted alongside the total number of branches and total runtime.
- L1 data-cache load misses are highest for B in this output, while overall cache misses are highest for A. These are different counters and should not be treated as interchangeable.
- Page-fault counts are close across the three implementations. This counter alone does not measure memory consumption.

These results describe this machine, compiler build, and workload. They are not universal guarantees. Use the uninstrumented five-run benchmark medians for the primary latency comparison.

## 8. Screenshots and evidence

The screenshot directory is organised as follows:

```text
screenshots/
├── correctness.png
├── benchmark-a.png
├── benchmark-b.png
├── benchmark-c.png
└── perf/
    ├── perf-a.png
    ├── perf-a-stats.png
    ├── perf-b.png
    ├── perf-b-stats.png
    ├── perf-c.png
    └── perf-c-stats.png
```
