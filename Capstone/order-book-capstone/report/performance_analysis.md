# Performance Analysis Report — Low-Latency Order Book

## 1. Methodology

- **Platform:** Windows 11, MSYS2 UCRT64, g++ 16.1.0
- **Build:** C++17 with `-O2`
- **Workload:** 1,000,000 operations, deterministic seed 42
- **Counts:** 700,471 ADD, 199,303 CANCEL, 100,226 BBO
- **Depth queries:** 10,000, performed every 100 operations; included in total time, not timed separately
- **Repetitions:** Five runs per implementation
- **Median:** Each metric is sorted and medianed independently

---

## 2. Results

| Metric              | A — `std::map` | B — Sorted Vector | C — Fixed Array |
|---------------------|:--------------:|:-----------------:|:---------------:|
| Median total time   | 473 ms         | 414 ms            | **226 ms**      |
| ADD latency         | 429.9 ns/op    | 309.2 ns/op       | **163.0 ns/op** |
| CANCEL latency      | 624.1 ns/op    | 716.6 ns/op       | **346.5 ns/op** |
| BBO latency         | **39.5 ns/op** | 44.9 ns/op        | 40.0 ns/op      |

All implementations reported checksum **508890804**, final best bid **99.99 × 2267**, and final best ask **100.01 × 2199**.

---

## 3. Interpretation

- **C (fixed array)** had the lowest median total time, ADD latency, and CANCEL latency in these measurements.
- **A (`std::map`)** had the lowest measured BBO latency. Its 0.5 ns/op difference from C is very small and may not represent a meaningful advantage without more controlled measurements.
- **B (sorted vector)** had the highest measured CANCEL latency in these runs. Inserting or removing a price level can require shifting elements.
- The matching checksum and BBO values support agreement on this deterministic workload, but do not replace the dedicated correctness test suite.

> These are workload- and environment-specific results, not universal performance guarantees.

---

## 4. Data-Structure Trade-offs

Let **L** be the number of active price levels, **N = 5000** the number of fixed slots per side for the supported price range, and **d** the requested depth.

| Operation  | A — `std::map` | B — Sorted Vector | C — Fixed Array |
|------------|----------------|-------------------|-----------------|
| **Add**    | O(log L) | O(log L) search + O(L) shifts for a new level | **O(1)** |
| **Cancel** | O(log L) | O(log L) search + O(L) shifts when a level becomes empty | O(1) average; O(N) worst case if the best level is rescanned |
| **BBO**    | O(1) | O(1) | O(1) |
| **Depth**  | O(d) | O(d) | Depends on how many slots must be scanned to find *d* populated levels |

### A — `std::map`

The tree maintains ordered price levels and supports a flexible set of active levels. Operations such as lookup and insertion are logarithmic in the number of active levels. Node-based storage may have less favorable locality than contiguous storage, but cache behavior was not measured directly here.

### B — Sorted Vector

Binary search locates a level in logarithmic time. Creating a new level or removing an empty level may require shifting later elements, which can make updates linear in the number of active levels. Contiguous storage can be cache-friendly, but does not eliminate shift costs.

### C — Fixed Array

The bounded tick range allows direct indexing by price, avoiding a search for a slot. If a best-price level becomes empty, the implementation may need to scan for the next occupied level. This is effective for the specified range but less flexible if the price range or tick model changes.

---

## 5. Benchmark Run Data

The complete five-run measurements are stored in `report/benchmark_results.txt`. The medians reported here are calculated independently for total runtime, ADD latency, CANCEL latency, and BBO latency.

---

## 6. Linux `perf` Status

The data in this report is from Windows/MSYS2. No Linux `perf` hardware-counter output was supplied, so this report does not claim measured cycles, instructions, branch misses, cache misses, or page faults.

If Linux profiling is required, run the following on Linux and record the actual output:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses ./bench --impl A
perf record -g ./bench --impl A
perf report
```

Repeat for `B` and `C`. If counters are unavailable in the environment, state that explicitly rather than inventing values.

---

## 7. Conclusion

For this Windows/MSYS2 workload, **C** was the strongest overall performer by median total time and ADD/CANCEL latency. **A** had the lowest measured BBO latency, with only a very small difference from C. The results should be interpreted in the context of the stated compiler, machine, and workload. Linux profiling results should be added separately if required by the assignment.