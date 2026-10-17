# Low-Latency Order Book: Performance Report

Nitish Kumar Bhambu | 24BCS10589 (roll 10589) | nitish.24bcs10589@sst.scaler.com

## Setup and correctness

Measured on 17 October 2026: Intel Core i5-12450H, Fedora Linux 7.2.10,
GCC 16.2.1, C++17, `-O2 -g -fno-omit-frame-pointer`. Runs were single-threaded,
pinned to CPU 0 (P core). Seed 42 generated exactly 1,000,000 operations per
replay: 700,471 adds, 199,304 live cancellations and 100,225 BBO queries. Both
depth-10 queries ran every 100 operations. Final live orders: 501,167.
Prices use integer ticks; there is no matching.

Directed API tests and 5,000 random operations passed, with BBO and full depth
compared after each operation and during a drain. Ring tests also force
wraparound, reuse and full-capacity rejection, including 3,000 random operations
at capacity seven. ASan/UBSan passed. All 60 full chrono/profiling replays matched
checksum 10326099508913568648; this is a consistency check, not a proof.

## Designs

**A:** sorted vectors hold orders by ID and price totals. Binary search locates
entries; cancelled orders remain with zero quantity. **B:** ordered maps hold
live orders by ID and price totals, using tree lookup. **C:** a circular queue
of free slot numbers reuses a preallocated order pool; an ID hash table finds
slots, and two price arrays with cached best indices provide BBO and depth.
Orders may be cancelled in any order: the ring manages free storage, not FIFO
order cancellation. Hash nodes still allocate.

Implementation C uses a preallocated order pool with a ring-buffer queue of reusable
slots, combined with flat price arrays for fast BBO updates. Its default capacity
is 600,000 simultaneous live orders; reaching full capacity throws without
overwriting orders. Capacity is configurable at construction. This bound and
its trade-offs are documented in the README.

## Results

Time rows are medians of five runs; operation rows use each run's mean.
Counter rows are means of five separate one-replay processes with explicit
P-core events. IPC and branch-miss rate are ratios of those mean counters.

| Metric | A: vectors | B: maps | C: ring slots |
|---|---|---|---|
| Replay time (ms) | 202.731 | 465.564 | 110.648 |
| With construction/destruction (ms) | 203.391 | 498.285 | 131.115 |
| ns / add | 129.321 | 333.799 | 59.184 |
| ns / cancel | 489.404 | 1,061.500 | 261.704 |
| ns / BBO | 26.792 | 31.496 | 23.872 |
| IPC | 0.916 | 0.688 | 1.164 |
| Branch misses (%) | 6.656 | 5.757 | 1.414 |
| Branch misses (count) | 10,114,901 | 17,483,336 | 2,183,104 |
| L1-dcache-load-misses | 30,166,766 | 17,877,107 | 4,732,568 |
| Cache misses | 16,004,947 | 13,172,995 | 6,090,218 |
| dTLB-load-misses | 1,324,847 | 1,453,308 | 527,222 |
| Page faults | 29,768 | 26,047 | 27,291 |

<!-- pagebreak -->

## Interpretation

**Speed.** C had the lowest measured add, cancellation and BBO means. Replay
medians were 202.731 ms (A), 465.564 ms (B) and 110.648 ms (C): C was about 1.83x
faster than A and 4.21x faster than B in this run. Direct price indexing and slot
reuse avoid tree searches and moving order storage. The 23.872 ns BBO result
is near the clock's measurement floor, so small BBO differences should not be
overinterpreted. All designs maintain their best levels for constant-time BBO.

**Setup cost.** C initializes its slot pool before the main timer. Including
construction/destruction gives 203.391 ms, 498.285 ms and 131.115 ms for A/B/C.
This second timer also includes per-run output and bookkeeping. Both timers
exclude workload generation. This makes the preallocation tradeoff visible;
the ring is not getting free initialization in the setup-inclusive comparison.

**Branch prediction.** B had the most branch misses (17.48 million), while A
had the highest miss rate (6.66%). Random ID searches choose directions in A's
binary search and B's trees. C's hash lookup and direct indexing require fewer
such decisions; its miss rate was 1.41%. The ring's head and tail wrap only
occasionally at this capacity. These explanations are consistent with the code
and counters, not attribution of each miss to a particular branch.

**Cache behavior.** A had the most measured L1 misses (30.17 million) and
generic cache misses (16.00 million). Its contiguous vector still receives
nonsequential binary searches through retained ID history and copies when it
grows. B follows separately allocated tree nodes. C had the fewest L1 and
generic cache misses: order slots are contiguous and price arrays total 80,000
bytes. C still follows hash nodes and then a slot index, so cancellation is not
just a single array access. Generic cache misses are not all cache levels summed.

**Page faults and memory.** A had the most page faults (29,768), followed by C
(27,291) and B (26,047). A retains history and grows vectors; C initializes a
fixed pool even before all slots are used. Separate single-replay measurements
showed peak process RSS of 90,160 KiB (A), 74,984 KiB (B) and 80,192 KiB (C).
All had zero major faults. These minor faults and RSS include the workload and
runtime; they are not book-only memory or evidence of a leak. C's default order
slots alone occupy 19.2 MB and its free-slot queue another 4.8 MB.

**Profiles.** A's sampled self time was 44.76% cancellation and 33.81% add. B's
was 43.04% add and 33.88% cancellation, with tree rebalancing visible. C's was
38.72% cancellation, 12.11% hash insertion and 10.23% add. Clock-reading code
was visible in all profiles. Sampling at 499 Hz recorded hundreds of samples
(about 1,000 for B), with zero lost samples. These percentages are approximate;
inlined helpers can appear under their caller. Raw call graphs are included.

## Production choice and limits

For this bounded price range and known order capacity, C is the strongest
starting point: simple ring operations, reusable storage and fast measured
updates. Before production, define safe capacity-exhaustion handling, reserve
or pool ID storage, and measure p99/p99.9 latency under cancellation bursts.
Hash allocation/rehashing and scans of up to 5,000 empty price slots still cause
variable work. A has growing history; B supports flexible ranges but has tree
and allocation overhead. Memory bounds and maintainability matter beyond means.

Perf counts the whole process, including generation, destruction and timers.
Chrono replay includes depth snapshots, checksums and per-operation clock costs.
All selected P-core counters ran 100% of enabled time. E-core events in the
generic list were inactive; E-core L1 misses were unsupported. Those messages
are retained, and the table uses available P-core events. This is one growing
workload on a shared desktop, so scheduling, frequency and cache state affect
comparisons. Raw measurement outputs and profiling data are preserved in `results/`.
