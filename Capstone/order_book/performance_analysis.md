# Order Book Performance Analysis

## 1. Benchmark Methodology

All three implementations were benchmarked using the same C++ benchmark, compiled with C++17 and `-O2`. The workload contains 1,000,000 operations, uses a fixed random seed, and consists of 70% add, 20% cancel, and 10% BBO operations. Bid and ask sides are selected equally, and top-10 bid and ask levels are queried every 100 operations. Cancellation selects a currently live order.

Five benchmark runs were performed for each implementation. The median total runtime is reported below. Hardware counters were collected separately using `perf stat -r 5`; those counters represent five whole benchmark-program invocations, with five internal benchmark runs per invocation.

## 2. Benchmark Results

| Metric | A: Ordered Maps | B: Heaps + Hash Maps | C: Fixed Arrays |
|---|---:|---:|---:|
| Median total time (ms) | 1092.12 | 953.062 | 539.568 |
| Average add latency (ns) | 895.784 | 657.416 | 321.303 |
| Average cancel latency (ns) | 1563.02 | 1135.14 | 942.825 |
| Average BBO latency (ns) | 78.3205 | 113.818 | 53.0507 |
| Checksum | 297281999578 | 297281999578 | 297281999578 |

Implementation C had the lowest median runtime. It was approximately 2.02 times as fast as A and 1.77 times as fast as B by median total runtime. C also had the lowest measured average latency for add, cancel, and BBO operations in these runs.

The identical checksums provide evidence that the benchmark workloads produced consistent results across implementations. Cross-implementation correctness was also checked separately.

## 3. Hardware Counter Results

The following counters are from `perf stat`, averaged across five whole-program invocations. Values are for the `cpu_core` counters where applicable.

| Metric | A | B | C |
|---|---:|---:|---:|
| Task-clock (ns) | 6,175,459,097 | 5,684,999,943 | 4,091,723,481 |
| CPU cycles | 7,769,485,605 | 6,852,570,288 | 4,799,141,628 |
| Instructions | 6,321,854,319 | 7,492,803,658 | 5,684,965,889 |
| IPC (instructions/cycle) | 0.81 | 1.09 | 1.18 |
| Branch misses | 42,011,161 | 9,872,607 | 7,931,205 |
| Branch misses (%) | 3.29% | 0.68% | 0.72% |
| Cache misses | 95,028,259 | 90,774,679 | 74,528,363 |
| L1 data-cache load misses | 86,026,535 | 104,116,356 | 35,071,104 |
| dTLB load misses | 6,591,224 | 6,435,123 | 5,375,638 |
| Page faults | 30,552 | 35,250 | 30,415 |

The `cpu_atom` L1 data-cache load-miss event was unsupported on this system; the L1 values above use `cpu_core` instead.

## 4. Interpretation

**Branch prediction:** A had the highest branch-miss percentage at 3.29%, compared with 0.68% for B and 0.72% for C. This is consistent with the extra branching and pointer-based traversal involved in ordered-map operations. B and C had lower branch-miss percentages in this workload.

**Cache behavior:** C had substantially fewer L1 data-cache load misses and fewer overall cache misses. Its fixed arrays provide contiguous, direct price-level access and avoid tree-node traversal. A's tree nodes and B's heap/hash-map structures involve more indirect memory access. B's L1 miss count was the highest despite its lower branch-miss rate, illustrating that branch prediction alone does not determine performance.

**IPC:** C achieved the highest IPC at 1.18, followed by B at 1.09 and A at 0.81. C retired more instructions per cycle in these measurements.

**Page faults:** B recorded 35,250 page faults, compared with 30,552 for A and 30,415 for C. These counts alone do not establish that B has a memory leak or that all faults were expensive; page faults can include normal memory allocation and first-touch effects. Further profiling would be needed to distinguish their causes.

## 5. Production Choice and Trade-offs

For this assignment's fixed price range and single-threaded workload, **Implementation C is the preferred performance choice**. It achieved the lowest measured total runtime and average latency for each timed operation, while also recording the fewest cache misses and the highest IPC.

However, this choice depends on the constraints. C reserves fixed arrays for the supported price range and scans them to find the best levels. If the price range changes substantially or becomes dynamic, this design may require more memory or changes to its indexing logic.

Implementation A is easier to reason about when sorted price levels and predictable ordered traversal are important. Implementation B provides fast typical best-price access, but stale heap entries can make individual BBO queries less predictable, and depth queries copy the heap. Its current implementation would need additional attention before choosing it for latency-sensitive production use.

These conclusions apply to the tested implementation and workload; they are not universal rankings for all order books.

## 6. Next Experiments

1. Measure latency percentiles, especially p99 and p99.9, to expose occasional slow operations.
2. Benchmark sparse and dense books, and vary the number of distinct active price levels.
3. Measure memory consumption and heap growth, including stale entries in B.
4. Profile allocations and investigate page-fault sources.
5. Repeat benchmarks with CPU affinity and a stable system load.
6. Explore a bitmap or hierarchical index over price levels to reduce array scanning while retaining direct indexing.

## 7. Conclusion

For the tested one-million-operation workload, Implementation C was the fastest overall. Its direct array indexing and cache behavior suit the fixed tick range. Implementation A offers sorted-map simplicity, while B offers heap-based best-price access but has extra cleanup and depth-query costs. A production decision should also consider latency tails, memory use, supported price ranges, and maintainability.
