# Order-book performance report

**Student:** 24BCS10070 — guna.24bcs10070@sst.scaler.com  
**Machine:** AMD Ryzen 7 6800H, Linux 7.2.10, GCC 16.2.1; C++17, `-O2`.

## Method and results

Five interleaved runs per implementation were pinned to CPU 0. Each replay used
the same seed-42 million-operation workload: 700,818 adds, 199,505 live-order
cancels, 99,677 BBO queries, and 10,000 pairs of depth-10 queries. All runs had
workload hash `4920528738431414721`, checksum `14235640582975296630`, and final
BBO 99.99 × 2747 / 100.01 × 2018. A separate full million-operation correctness
run passed, comparing all levels after every operation. An independent
recomputing oracle and deterministic edge cases also passed.

Time metrics below are medians of five runs. Replay totals exclude generation,
construction and destruction. Per-operation averages come from separate fresh-book
replays and include clock overhead. Counter counts are `perf stat -r 5` means,
collected one event at a time to avoid multiplexing. IPC and branch-miss rates
are ratios of those means from separate processes. `perf` includes the entire
process, unlike replay timing. Turbo and frequency were not fixed.

| Metric | A: hash + maps | B: tree + vectors | C: flat table + arrays |
| --- | ---: | ---: | ---: |
| Total replay time (ms) | 164.507 | 215.782 | 63.003 |
| ns / add | 157.610 | 125.367 | 110.409 |
| ns / cancel | 282.584 | 577.288 | 80.180 |
| ns / BBO | 24.000 | 22.254 | 22.492 |
| IPC | 0.862 | 1.039 | 1.479 |
| Branch misses (%) | 5.131 | 5.364 | 3.199 |
| L1-dcache-load-misses | 16,510,746 | 27,170,206 | 8,440,874 |
| Cache misses | 7,559,064 | 6,477,848 | 5,248,880 |
| dTLB-load-misses | 579,139 | 1,050,447 | 11,166 |
| Page faults | 6,728 | 8,202 | 3,439 |

## Latency and sampled hotspots

C has the lowest measured add and cancellation averages and approximately 2.61×
lower total replay time than A. B's BBO median is marginally lowest, but the
0.24 ns difference from C is too small to treat as a robust win given timer
overhead and system noise. Every design reads maintained endpoints or cached
indices rather than searching the whole book. Initial timing runs were slower;
we retained them and used medians rather than selecting favorable samples.

User-space call graphs over ten replays attribute A's self samples primarily
to add (38.54%), cancel (30.23%), and standard hash insertion (11.57%). B spends
35.39% in cancellation, 20.00% in level binary search, and 10.85% in tree erase
rebalancing. C spends 66.23% in flat-table insertion and 9.35% in ID lookup,
including insertion work reached from rebuilds. Thus even C's fastest price
storage leaves order indexing as a major optimization target. These are sampled
self percentages, not exact per-method elapsed times.

## Branches, caches and memory

B has the highest observed branch-miss rate and absolute branch misses. Its
ID-tree search, rebalancing, and level binary searches follow data-dependent
paths. C's direct level addressing avoids those searches, though probing still
branches. These structural explanations are consistent with the counters; the
counters alone do not identify a particular source line as causal.

B has the most L1 misses, but A has the most generic cache misses. They measure
different events, so these rankings need not agree. B's contiguous levels help
traversal, while its individual orders remain allocated tree nodes. A follows
nodes in both order and price structures. C uses contiguous slots, quantity
arrays, and occupancy words; its much lower dTLB misses support a locality
advantage in this workload. The book becomes dense quickly, so vector shifts
and bitmap scans are less prominent than in a sparse churn-heavy workload.

B also has the most page faults; C has the fewest. Node allocation and repeated
first touches plausibly contribute. Fault counts are not peak-memory sizes:
allocation policies and huge pages also affect them. C retains high-water table
capacity and temporarily holds two tables during growth, so fewer faults do not
prove lower peak memory.

## Production choice and next experiments

For this fixed price band, I would choose C after pre-sizing order capacity,
validating collision behavior, and measuring tail latency. Its price storage is
bounded and queries predictable, but the current growing table has rehash spikes
and more custom code to maintain. A is the simpler reference for auditing.
Deployment confidence needs more than these averages: memory limits, allocation
failure handling and p99/p99.9 cancellation/add latency remain important.

Next experiments: reserve against a known order bound; separate slot metadata
from payload; reuse depth buffers; measure sparse books, repeated best-level
removal and adversarial ID patterns; record peak RSS and tail latency. All
requested user-mode counters and call graphs were available. Explicit
`--user-callchains` resolved the initial kernel-profiling restriction without
changing kernel settings.

Evidence: [raw timings](results/benchmarks.csv), [raw counters](results/perf_events.txt),
[derived counters](results/counters.json),
[A stat](results/perf_stat_A.txt), [B stat](results/perf_stat_B.txt),
[C stat](results/perf_stat_C.txt), and
[A](results/perf_report_A.txt)/[B](results/perf_report_B.txt)/[C](results/perf_report_C.txt)
call graphs. Reproduction commands and complexity analysis are in the README.
