# Performance Postmortem — Low-Latency Order Book

**Name:** Mohammed Rehan  
**Email:** mohammed.24bcs10620@sst.scaler.com  
**Roll number:** 10620

> Status: template. Replace every bracketed field with measurements and observations from the machine used for the final run. Do not infer hardware-counter values from elapsed time.

## 1. Test environment

- Date/time: [fill in]
- Machine / CPU: [fill in]
- RAM: [fill in]
- OS / kernel: [fill in]
- Compiler and version: [output of `g++ --version`]
- Build flags: `-std=c++17 -O2`
- Workload: 1,000,000 operations, seed 42, five benchmark runs; report medians
- Correctness: [paste `./bench --verify` result]

## 2. Results table

Populate timing cells from the median output of `./bench --impl X` and profiling cells from `perf stat`. Use `N/A (unsupported)` where a counter is not available.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | [ ] | [ ] | [ ] |
| ns / add | [ ] | [ ] | [ ] |
| ns / cancel | [ ] | [ ] | [ ] |
| ns / BBO | [ ] | [ ] | [ ] |
| IPC (`instructions / cycles`) | [ ] | [ ] | [ ] |
| Branch misses (%) | [ ] | [ ] | [ ] |
| L1-dcache-load-misses | [ ] | [ ] | [ ] |
| Cache misses | [ ] | [ ] | [ ] |
| dTLB-load-misses | [ ] | [ ] | [ ] |
| Page faults | [ ] | [ ] | [ ] |

## 3. Which implementation was fastest?

- Add: [winner, measured value, likely explanation]
- Cancel: [winner, measured value, likely explanation]
- BBO: [winner, measured value, likely explanation]
- Total workload: [winner, measured value]

Interpret these in the context of the workload. A is expected to pay for scanning unordered price levels during BBO queries; B pays for tree operations but can read best prices from ordered endpoints; C uses direct level indexing and a bounded bitmap search. These are hypotheses based on the designs, not a substitute for the actual measurements.

## 4. Branch prediction

- Highest branch-miss count/rate: [implementation and measurement]
- Relevant hot path from `perf report`: [function/symbol]
- Explanation supported by code and measurements: [fill in]

Do not equate the largest raw count with the worst rate without considering total branches and workload time.

## 5. Cache and memory behavior

- Highest cache-miss count/rate: [implementation and measurement]
- Highest L1 data-cache load misses: [implementation and measurement]
- Highest dTLB load misses: [implementation and measurement]
- Memory-layout explanation: [contiguous arrays vs tree nodes/hash-table buckets, pointer chasing, active-level distribution, and any relevant `perf report` evidence]

The array-based implementation uses contiguous fixed-size quantity arrays; `std::map` typically allocates individual tree nodes; `unordered_map` uses buckets and separately stored elements. Confirm whether the measured counters support the expected effect on this machine.

## 6. Page faults

- Observed counts: [fill in]
- Likely explanation: [fill in]

Page-fault counts alone do not directly measure total memory use. Distinguish minor from major faults if the available tooling reports that information.

## 7. Production choice

**Choice:** [A / B / C]

Discuss all six dimensions, not just average latency:

- **Latency:** [measured result, including the relevant operations]
- **Predictability:** [worst-case or bounded work, allocation behavior]
- **Memory usage:** [fixed arrays versus dynamic containers; quantify if measured]
- **Cache behavior:** [counter evidence and layout]
- **Complexity:** [implementation and invariants]
- **Maintainability:** [bounds, correctness risks, ease of changes]

## 8. What I would try next

Choose a small number of experiments and state the hypothesis before measuring:

1. [Optimization / hypothesis]
2. [Optimization / hypothesis]
3. [Optimization / hypothesis]

Possible experiments: pre-reserving hash-map capacity, measuring allocator effects, using a compiler-supported count-leading/trailing-zero operation for bitmap searches, caching best-price state, or changing the order-ID index. Re-run correctness tests after each code change and compare medians under the same workload.

## 9. Limitations

- Per-call `steady_clock` measurements include timer overhead.
- [Document machine load, power profile, thermal state, VM/container limits, or other confounders.]
- [List counters unavailable from `perf`, with the exact error.]
- [Record any code changes made after the measurements.]

## 10. Evidence

- Correctness screenshot: [`../screenshots/correctness.png`](../screenshots/correctness.png)
- Implementation A screenshot: [`../screenshots/benchmark-a.png`](../screenshots/benchmark-a.png)
- Implementation B screenshot: [`../screenshots/benchmark-b.png`](../screenshots/benchmark-b.png)
- Implementation C screenshot: [`../screenshots/benchmark-c.png`](../screenshots/benchmark-c.png)
