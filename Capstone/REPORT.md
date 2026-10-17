# Benchmark and Performance Analysis

## Correctness

Run `./bench --verify` before measuring. A, B, and C all passed the same fixed-seed workload comparison, including empty-book BBO and unknown-cancel checks. Exact verification output:

```text
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## Timing results

Measured on an Apple Silicon arm64 Mac (`Darwin 27.0.0`), compiled with `g++ -std=c++17 -O2 -Wall -Wextra -pedantic`. These are local measurements, not Linux perf results. Benchmark runs each implementation five times internally; values shown are its reported median.

| Metric | A: map + FIFO | B: sorted vector | C: indexed array + heaps |
|---|---:|---:|---:|
| Total time (ms) | 143.519 | 95.782 | 110.865 |
| ns / add | 111.441 | 77.9304 | 30.6051 |
| ns / cancel | 235.957 | 116.506 | 82.0108 |
| ns / BBO | 19.017 | 13.5194 | 15.7245 |

All three implementations returned:
- Best bid: 99.99 x 2722
- Best ask: 100.01 x 2525
- Checksum: 4495818852

## Linux perf results

This development environment is macOS (Darwin arm64), so Linux `perf stat` and `perf record` cannot run here. Counters and call graphs are unavailable and are not estimated. Run the commands in `README.md` on Linux and fill in actual measurements before making claims based on hardware counters.

| Metric | A | B | C |
|---|---:|---:|---:|
| IPC | — | — | — |
| Branch misses (%) | — | — | — |
| L1-dcache-load-misses | — | — | — |
| Cache misses | — | — | — |
| Page faults | — | — | — |

## Analysis guide

In this run B had the shortest total time (126.610 ms), while C had the lowest measured add latency (61.701 ns). B also had the lowest cancellation latency (243.086 ns) and BBO latency (17.262 ns). These results are specific to this Apple Silicon host and this workload. B's contiguous level vector performs well at the modest number of active levels, but its O(L) insertions and removals can become expensive as more distinct prices are populated. C's direct-index add path avoids ordered-tree lookup and vector shifts, which is consistent with its strong add result. Its total run includes periodic depth snapshots, each of which scans the tick-indexed arrays; that work is outside the per-operation add/cancel/BBO timers and contributes to total time. A's map and per-order list have extra node traversal/allocation overhead, consistent with the slower aggregate result in this run. The small BBO timing differences are close and should not be overinterpreted.

The counters needed to identify branch prediction, cache misses, IPC, and page faults could not be collected on this Mac. Therefore no conclusion about which implementation has the most branch misses, cache misses, or page faults can be supported yet. Structurally, A uses pointer-linked nodes, B uses compact vectors for levels, and C keeps fixed level arrays plus heap and hash storage; this motivates hypotheses about locality and allocation, but does not replace measurements.

For a production choice based only on the available results, B is the current provisional choice: it led total time and cancellation/BBO latency while being relatively simple to inspect. C may be preferable when add latency dominates and the fixed price range is stable, but its scanning depth queries and lazy heaps need validation under production workloads. No final choice should be made until Linux perf data and a workload representative of deployment confirm latency distributions, memory use, and tail behavior. Next experiments: benchmark varied active-level counts and workload mixes, record percentile/tail latency, collect Linux perf counters, pre-size ID hash tables, and compare a bitmap-assisted fixed array for fast depth traversal.

Once results are collected, answer: which wins for each operation and overall; whether branch misses track control flow or workload effects; whether cache misses align with pointer-heavy versus contiguous storage; which design faults/allocates most; and which design best balances latency, predictability, memory use, complexity, and maintenance for a production choice. Possible next experiments include reserving hash tables, using a flat ID table, pooling order nodes, and comparing an indexed bitmap or hierarchical bitset for depth/BBO.
