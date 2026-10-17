# Order Book Performance Report

Prabhav Semwal — 24bcs10358 — prabhav.24bcs10358@sst.scaler.com

## Method and correctness

These measurements were collected on 17 October 2026 in a Debian 12 container inside this Apple M5 machine's Colima ARM64 Linux VM (8 vCPUs). GCC 12.2.0 compiled with C++17, `-O2 -g -fno-omit-frame-pointer`. CPU placement, frequency and background load were uncontrolled. Each timing column is the median of five trial averages. A trial contains 1,000,000 operations: 700,829 adds, 199,496 live-order cancels and 99,675 BBO queries, plus depth-10 reads of both sides every 100 operations. All books use the same seed-42 trace.

Verification passed after every operation for BBO/top-ten levels, with periodic full-depth comparisons and a separate independent full-depth reference test. The full verifier also passed ASan/UBSan. Every benchmark produced bid **99.99 x 3244**, ask **100.01 x 2193**, checksum **4466782172639580222**. Raw logs are in `results/`.

## Results

| Metric | A: maps | B: sorted vectors | C: arrays + flat hash |
| --- | ---: | ---: | ---: |
| Total time (ms) | 212.629 | 105.280 | 90.816 |
| ns / add | 137.175 | 78.894 | 83.783 |
| ns / cancel | 468.109 | 150.327 | 57.772 |
| ns / BBO | 15.237 | 14.976 | 15.091 |
| IPC | N/A | N/A | N/A |
| Branch misses (%) | N/A | N/A | N/A |
| L1-dcache-load-misses | N/A | N/A | N/A |
| Cache misses | N/A | N/A | N/A |
| Page faults, perf mean | 16,951 | 16,268 | 25,305 |
| Task-clock (ms), perf mean | 175.92 | 121.48 | 57.27 |
| Maximum RSS (KiB), separate replay | 66,388 | 63,700 | 91,400 |

The VM does not expose a hardware PMU. `perf stat -r 5` reports cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses and dTLB-load-misses as **not supported** for all three books. IPC and branch-miss rates cannot be calculated. Task-clock and page-faults work. Their process-wide counts include generation, construction and teardown. The replay timer excludes those phases and includes per-operation clock overhead; the two measurements describe different scopes. BBO times are close to timer overhead, so sub-nanosecond differences are not persuasive rankings.

## 11.1 Fastest implementation and sampled hotspots

C has the lowest total time, about 2.34 times faster than A and 14% less time than B. B has the fastest adds; C has the fastest cancels. B has the lowest nominal BBO average, but all three are effectively close in this measurement. C's direct quantity indexing avoids searching price levels, while its order hash growth/probing adds work to insertion. B usually updates existing levels through binary search after the book becomes dense; the O(L) shifts are less frequent than the worst-case complexity suggests.

`perf record` sampled user task-clock at 9,999 Hz with call graphs and no lost samples: 1,865 samples for A, 1,025 for B and 529 for C. A spends 38.34% self samples in cancellation and 34.42% in addition; tree rebalancing is also visible. B spends 30.54% in addition and 24.68% in cancellation. C spends 38.37% in `FlatOrders::find` and 5.29% in growth. These locate broad costs; short runs and inlining limit precision.

## 11.2 Branch prediction

There is no measured branch-miss winner because the required events are unavailable. A's tree navigation and B's binary searches branch on data-dependent comparisons. C's collision probing and tombstone checks also branch on occupancy. Those are candidates for misprediction, not measured explanations for the timing gap. The call graphs establish where CPU time went, not which branches were mispredicted.

## 11.3 Cache behavior

Cache-miss counts are also unavailable, so no empirical ranking is possible. A follows separately allocated tree nodes for both IDs and prices. B stores levels contiguously but still allocates hash nodes for orders. C stores both quantities and ID slots contiguously, reducing pointer chasing while potentially touching a larger table. These layouts plausibly affect locality; elapsed time alone does not prove fewer cache misses.

## 11.4 Page faults

C has the most page faults and the highest peak RSS. Its table repeatedly allocates larger vectors, initializes slots and temporarily keeps the old and new tables during growth. This agrees with the observed growth hotspot. B has the lowest values in both columns. RSS includes the common workload, allocator overhead and temporary storage; page faults are page-touch events, not a direct count of allocation calls or cache misses.

## 11.5 Production choice

I would initially choose B for this bounded instrument: its total time is about 16% above C, adds are faster, and its measured process RSS is lower. Standard-library order storage and straightforward sorted levels are easier to maintain than a new hash table. Contiguous levels also make depth reads simple. Predictability remains a concern: vector shifts, hash growth and node allocations can create spikes. C is attractive for cancellation-heavy traffic, but its rehashing, tombstones and bounded best-price scans need tail-latency testing. Neither should be selected solely from these averages.

## 11.6 Next experiments

Measure p99/p99.9 latency and isolated book memory on bare-metal Linux with hardware counters. Reserve order capacity before the replay and compare the memory/latency tradeoff. For C, avoid the repeated ID lookup during insertion/cancellation and test same-size tombstone cleanup before growing. Add a nonempty-price bitmap to bound sparse best-price/depth searches more tightly. Vary live-order count and cancellation intensity, then collect longer call graphs. Integer ticks and fixed quantity arrays are already used here.
