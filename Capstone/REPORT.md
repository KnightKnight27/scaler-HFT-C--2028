# Benchmark and Performance Analysis

**Team size:** 2

| Student | Roll number | SST email |
|---|---|---|
| Tanush Shoor | 24BCS10265 | tanush.24bcs10265@sst.scaler.com |
| Vivaan Goyal | 24BCS10163 | vivaan.24bcs10163@sst.scaler.com |

## Correctness

The three implementations were checked against the same deterministic one-million-operation workload. Verification compares BBO, top-ten bid and ask levels, prices, quantities, and cancellation results after each operation. Empty-book BBO and cancellation of an unknown ID are also checked.

~~~text
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
~~~

## Benchmark method and results

The workload uses seed 42 and approximately 70% add, 20% cancel, and 10% BBO operations, plus depth-10 bid and ask reads every 100 operations. Cancels select live order IDs.

Each ./bench --impl command runs five internal trials and reports the median. The final supplied log contains five such benchmark summaries per implementation, emitted during perf stat -r 5. The values below are the median of those five reported summaries. This keeps one representative benchmark value per implementation while retaining the repeated-run information in the source output.

| Metric | A: map + FIFO | B: sorted vector | C: indexed array + heaps |
|---|---:|---:|---:|
| Total time (ms) | 326.448 | 210.859 | 248.618 |
| ns / add_order | 280.337 | 162.991 | 88.1522 |
| ns / cancel_order | 440.064 | 295.886 | 244.111 |
| ns / get_bbo | 54.3794 | 46.4098 | 50.8166 |

The five total-time summaries ranged from 317.713 to 338.222 ms for A, 209.306 to 219.112 ms for B, and 244.450 to 256.582 ms for C. All runs returned the same final BBO: bid 99.99 x 2722 and ask 100.01 x 2525. All printed checksum 4495818852.

B was fastest overall and for BBO. C had the fastest add and cancel averages. A was slowest on all four reported timing metrics. These results come from the teammate's Linux environment; machine and system load affect them.

## perf stat measurements

The final perf stat -r 5 output supplied for all three implementations reports these event counts. Each perf summary is averaged across five process runs. The event running percentages shown by perf are about 85.7%, which indicates the events were multiplexed; treat derived ratios as approximate.

| Counter / metric | A | B | C |
|---|---:|---:|---:|
| task-clock (ms) | 2,528.28 | 1,384.94 | 1,524.47 |
| cycles | 7,778,147,865 | 4,256,542,659 | 4,687,678,854 |
| instructions | 5,255,009,624 | 4,447,293,699 | 12,281,165,270 |
| branches | 1,131,777,044 | 845,317,928 | 2,947,340,160 |
| branch-misses | 47,960,523 | 48,901,404 | 8,851,277 |
| branch-miss rate | 4.238% | 5.785% | 0.300% |
| cache-misses | 91,668,067 | 22,889,005 | 27,123,166 |
| L1-dcache-load-misses | 137,597,600 | 120,539,509 | 167,885,161 |
| dTLB-load-misses | 12,965,039 | 2,836,068 | 3,117,457 |
| page-faults | 12,660 | 6,758 | 6,734 |
| elapsed time (s) | 2.532980 ± 0.60% | 1.388039 ± 0.43% | 1.527927 ± 0.67% |
| IPC (instructions / cycles) | 0.676 | 1.045 | 2.620 |

IPC and branch-miss rate are calculated from the perf counts:
- IPC = instructions / cycles.
- Branch-miss rate = branch-misses / branches.

The event percentages printed by perf are approximately 85.7% for the hardware counters; the counts are therefore scaled estimates from multiplexed events. perf reported a small run-to-run spread: task-clock ±0.60% for A, ±0.44% for B, and ±0.67% for C. Page faults varied by ±0.99%, ±1.85%, and ±1.86%, respectively. The final output supplied values for every requested event, so no counter is marked unsupported or missing.

## perf record and perf report

Earlier supplied perf report screenshots show the call-graph sampling completed without lost samples:

| Implementation | Samples | Lost samples | Profile event |
|---|---:|---:|---|
| A | 6,956 | 0 | task-clock:u |
| B | 3,773 | 0 | task-clock:u |
| C | 4,496 | 0 | task-clock:u |

The visible top sample shares were 16.66% for A, 45.48% for B, and 34.05% for C. The displayed symbols were unresolved addresses or [unknown], so these percentages cannot be attributed confidently to specific source functions. The perf stat counters provide the more useful cross-implementation comparisons below.

## Performance analysis

### Operation latency and overall time

B produced the lowest total time (210.859 ms), about 15.2% below C and 35.4% below A in this workload. It also had the fastest BBO average (46.4098 ns). C had the fastest add (88.1522 ns) and cancel (244.111 ns) averages. Its add average was about 45.9% below B's, and its cancel average about 17.5% below B's.

This split matches the designs' tradeoffs. C indexes quantities directly by tick and looks up order IDs through a hash map, avoiding sorted-vector shifts during add and cancel. B's sorted level vector makes the best prices available at the endpoints and offers compact level storage. Its linear shifting cost did not outweigh those benefits for the active-level distribution in this workload. A's ordered tree and per-level linked lists maintain ordering and order locators, but incur extra node traversal and allocation costs. Those are plausible reasons for its slower results, not conclusions isolated by the measurements.

### Branch prediction

B had the highest branch-miss rate at 5.785%, followed by A at 4.238%; C was lowest at 0.300%. B also had the highest absolute branch-miss count (48.9 million, slightly above A's 48.0 million). C executed many more branches (2.95 billion) yet missed less often, giving it a very low miss percentage. A plausible explanation is that the fixed-array scan and heap paths have more regular control flow, while binary search and vector-level maintenance in B and tree navigation in A make less predictable decisions. This is an interpretation of the counter pattern, not a source-level finding; the supplied perf report symbols are unresolved.

### Cache behavior

A had the most cache misses (91.7 million) and dTLB load misses (13.0 million), consistent with the pointer-heavy map/list design touching separately allocated nodes. B had the fewest cache misses (22.9 million) and L1 data-load misses (120.5 million). Its contiguous vector of active levels is likely to help locality. C had 27.1 million cache misses, between A and B, but the most L1 data-load misses (167.9 million). Its depth operation scans a fixed tick-indexed range, which is a plausible contributor to the higher L1 load-miss count. The counters support these comparisons, while the unresolved call graphs prevent pinpointing individual code paths.

### Page faults and memory use

A had 12,660 page faults, nearly twice the counts for B (6,758) and C (6,734). This fits the expectation that A's tree and per-order list nodes touch more separately allocated memory. B and C have similar page-fault counts in this run. Page faults measure page-level memory events, not total memory consumption, so memory footprint should be measured separately before making a production claim.

### IPC and production choice

C had the highest IPC (2.620), B measured 1.045, and A 0.676. C also executed the largest instruction and branch counts, likely reflecting work such as scanning the fixed tick range for depth; a high IPC does not mean it performed the least total work. The values are approximate because perf multiplexed the events.

For this tested workload, B is the provisional production choice. It offers the best total time and BBO latency, the fewest cache misses, and the simplest level traversal. C is a strong alternative when add and cancel latency are the dominant requirements and the fixed price range is suitable. B's O(L) vector insert/erase can create latency spikes as the active-level count grows; C's fixed-range scans and lazy-heap cleanup also need tail-latency evaluation. Before deployment, compare percentile latency, memory footprint, and realistic traffic mixes, rather than relying only on averages.

## Further experiments

Repeat the workload at several active-level counts and operation mixes, and record p50/p99/p99.9 latency. Measure memory footprint and allocation count. Compile with -O2 -g to improve perf report symbol resolution while retaining optimization, then profile again. Consider pre-sizing hash tables, a bitmap-assisted fixed array for faster depth queries, and object pooling for A. Keep the same compiler flags, workload, and host conditions across comparisons.
