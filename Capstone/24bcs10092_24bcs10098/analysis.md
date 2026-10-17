# Performance analysis

The three resting order books implement the same five APIs, integer ticks,
aggregated depth and permanent ID uniqueness. The comparison uses the mandatory
seed-42 trace: exactly 700,000 adds, 200,000 live cancellations, 100,000 BBO calls,
plus 10,000 depth(10) queries per side. All five replays per implementation agreed
on checksum, final BBO and full final depth. Numbers below come only from
[benchmark.md](benchmark.md) and [profiling.md](profiling.md).

## Designs and expected costs

Let N be live orders, L occupied side levels, K returned levels, U lifetime
accepted IDs, and W=79 bitmap words per side. Hash costs are expected amortized;
collisions, rehashing and allocation can produce spikes.

| Design | Add | Cancel | BBO | Depth |
| --- | --- | --- | --- | --- |
| A: ordered map levels, FIFO lists, ID locators | O(log(L+1)) | O(1) amortized | O(1) | O(K) |
| B: indexed order heaps, aggregate hash tables | O(log(N+1)) | O(log(N+1)) | expected O(1) | O(L log(L+1)) |
| C: fixed tick arrays, occupancy bitmap, FIFO lists | expected O(1) | expected O(1) | O(W) | O(W+K) |

## Observed five-run medians

Operation columns are medians of per-run mean nanoseconds, not individual
latency medians. Overall milliseconds include depth and harness overhead.

| Design | Overall ms | Add ns | Cancel ns | BBO ns | Depth total ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| A | 429.377 | 430.053 | 466.571 | 26.339 | 4.156 |
| B | 6737.518 | 505.433 | 1247.221 | 55.127 | 6063.265 |
| C | 327.892 | 267.270 | 490.410 | 46.780 | 4.388 |

C had the lowest total for this trace. Direct tick indexing avoids A's ordered
price-tree lookup and B's heap repair; its bit traversal produces shallow depth
without sorting every occupied level. Its lower measured add mean matters for
700,000 additions. This is an architectural explanation consistent with the
measurements, not a controlled causal decomposition. C did not win every API:
A had lower cancellation and BBO means and slightly lower total depth time.

B's required depth queries cost a median 6063.265 ms versus 6737.518 ms overall.
Its actual user-cycle profile attributes 43.66% self overhead to introsort and
43.57% to depth, with a chain through get_asks -> depth -> sort -> introsort.
That strongly supports depth copying/sorting as the dominant work. Required
queries were retained; its cancellation locator also requires updates during
heap swaps. Independent metric medians should not be added to reconstruct totals.

## Memory and hardware evidence

All designs retain O(U) accepted-ID history after cancellation. The trace ends
with 500,000 live orders but has accepted 700,000 IDs. C additionally allocates
10,000 fixed level records, even when empty; its FIFO nodes still allocate.
A has tree/list nodes; B has heap-vector capacity and aggregate hashes, retaining
capacity after removals. C is not an allocation-free design.

Median process peak RSS was A 117884, B 117320 and C 117504 KiB. These include
the trace, allocator retention and untimed passes of all implementations, so they
cannot establish per-book memory rankings. Replay fault deltas were zero after
those warming passes; this does not mean zero allocation or cold-start faults.

Perf's whole-process branch-miss rates were A 2.556%, B 15.056%, C 1.077%.
B's much larger branch/instruction counts and sorting profile support its
extra-work explanation. L1-data load-miss rates were 8.547%, 8.362%, 7.241%;
dTLB load-miss rates were 0.723%, 0.236%, 0.691%. B's smaller dTLB fraction does
not imply fewer misses: its load denominator is much larger. Generic cache-miss
rates (64.376%, 15.602%, 71.545%) use cache references, not all loads, and do not
alone rank performance. L1 data-cache misses, translation-cache misses and OS
page faults concern different mechanisms; none should be equated with disk I/O.

## Recommendation and limits

C is the preferred production-oriented starting point **for this assignment's
fixed 5,000-tick-per-side workload**. A is a readable alternative when price
ranges need flexibility. B illustrates fast best-price access but its current
depth strategy is expensive for this required query mix. These are starting
points, not production-readiness certifications: budget permanent ID history,
consider allocation control, inject allocation failures, and test tail latency.

Results were collected under Ubuntu WSL2 on an Intel i3-1115G4 using GCC 15.2,
C++17 and -O2. Per-call clock reads, virtual dispatch and checksum bookkeeping
perturb short-operation measurements. Fresh books do not create cold allocator,
cache or OS state. There was no affinity/frequency control or host isolation.
Perf profiles use -g/frame pointers and include setup, selected replay, checks
and destruction; they are not the chrono replay-only window. All counters
reported 100% running time, but virtualization and finite samples remain limits.
Repeat on controlled native Linux before making broader performance claims.
