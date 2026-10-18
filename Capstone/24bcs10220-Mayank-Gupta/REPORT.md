# Order-book comparison

## Setup and correctness

Measured on 16 October 2026 in a local Ubuntu 26.04.1 ARM64 VM on an Apple M5 Pro
Mac. Lima 2.2.1 used Apple's VZ backend, two vCPUs and 4 GiB RAM. The program was
single-threaded, compiled with GCC 15.2.0, C++17, `-O2 -g -fno-omit-frame-pointer`.
Linux was 7.0.0-38-generic; perf was 7.0.14. See
[`results/linux/environment.txt`](results/linux/environment.txt).

All three books passed known-answer edge cases and 20,000 random operations
against a reference that rebuilds levels from individual orders. Every operation
was checked against the full book, followed by a drain-to-empty test. Address and
undefined-behaviour sanitizers passed on Linux and macOS.

The benchmark used one million operations with seed 42: 700,471 adds, 199,303
cancels and 100,226 BBO queries, plus 10,000 depth-10 queries per side. Each book
started empty. All three finished with 501,168 live orders and checksum
`4513379934266391350`. Final BBO: bid `99.99 x 2722`, ask `100.01 x 2525`.

## Results

Timing entries are medians of five runs; per-operation entries are medians of the
five run averages. Perf counters are means from `perf stat -r 5` on `--profile`,
which disables per-operation timers. Raw runs are in
[`results/linux`](results/linux) and [`results/linux-perf`](results/linux-perf).

| Metric | A: maps | B: sorted vectors | C: indexed arrays |
|---|---:|---:|---:|
| Total timed replay (ms) | 212.17 | 116.10 | 78.84 |
| ns / add | 131.23 | 86.77 | 39.02 |
| ns / cancel | 492.28 | 179.54 | 160.79 |
| ns / BBO | 16.66 | 14.57 | 13.88 |
| IPC | N/A | N/A | N/A |
| Branch misses (%) | N/A | N/A | N/A |
| L1 data-cache load misses | N/A | N/A | N/A |
| Cache misses | N/A | N/A | N/A |
| dTLB load misses | N/A | N/A | N/A |
| Page faults, whole process | 2,149 | 2,723 | 3,219 |
| Task-clock, whole process (ms) | 166.11 | 96.64 | 60.36 |

The guest exposes no hardware PMU. Cycles, instructions, branches, branch misses,
cache misses, L1 misses and dTLB misses explicitly returned `<not supported>`.
Consequently IPC and branch-miss percentages cannot be calculated. `sudo perf`
was used because the guest restricts unprivileged profiling; its permission
setting was left unchanged. Call graphs used `perf record -e cpu-clock -g`.

The total replay includes timer/checksum overhead and depth queries, but excludes
workload generation and book destruction. Perf covers the whole process, including
those excluded stages. These two totals are not directly comparable. BBO readings
are near clock-call overhead; their small differences should not be interpreted
as precise intrinsic latency. VM scheduling also adds noise: C's timed totals
ranged from 73.50 to 85.23 ms. No tail-latency claim follows from these averages.

## What explains the results?

C had the lowest median for all three operations and a total about 2.69 times
faster than A. Its direct price lookup avoids tree traversal and binary search.
Cancellation still needs an ID hash lookup, a vector-slot move and repair of the
moved order's lookup, so its advantage over B was much smaller for cancellations
than additions. All three BBO implementations read maintained endpoints in
constant time.

The software profiles support the search-cost explanation. A attributed about
37% of samples directly to `MapBook::add_order`; its call graph also shows ID and
price-tree searches inside inlined cancellations. B attributed about 36% to add
and 22% to cancel, largely in binary searches. C's largest named book hotspot was
cancellation at about 20%; hash insertion and `malloc` were also visible. These
were short samples (619, 418 and 249 respectively, with no lost samples), so the
percentages identify broad hotspots rather than precise costs.

The benchmark changes both order and level storage between designs. It does not
isolate the causal effect of only the price container. Also, 70% adds versus 20%
cancels steadily fills a fixed price grid. Later operations usually update
existing levels. That reduces B's vector-shifting cost and C's empty-price scans;
a sparse, high-churn workload could behave differently.

## Branches, caches and page faults

The unavailable hardware counters prevent ranking branch or cache misses.
From the code, A follows separately allocated tree nodes; B searches contiguous
levels but keeps orders in hash nodes; C has contiguous price totals and packed
order records but still allocates hash nodes. Random tree and binary-search
decisions are plausible sources of branch mispredictions. These are hypotheses,
not measured branch/cache results. The two price arrays together occupy 80,000
bytes, so even C's price storage should not be assumed to fit in L1.

C had the most page faults in this run. Growing both its packed-order vector and
its ID table is a plausible contributor, while A and B have different allocation
patterns. Whole-process counts also include the workload and runtime. Page faults
alone do not establish peak memory use, disk I/O, or a cache-miss ranking.

## Choice and next experiments

For these fixed bands, C is the candidate to develop further: simple direct price
updates, constant-time BBO and compact level storage. It is not ready for a
production latency guarantee. Vector growth, hash rehashing and scanning after
best-level removal can cause spikes, and its separate slot index adds memory and
maintenance cost. A is easier to extend to arbitrary prices; B is a useful middle
ground with straightforward sorted output. Peak memory and latency tails need
measurement before an actual deployment choice.

Next experiments would reserve a documented order capacity, measure allocation
counts and peak RSS, and test sparse books, repeated best-level removals and
constant live-order counts. An occupied-level bitmap could bound C's scans if
those prove expensive. Hardware-counter measurements on bare-metal Linux and
p99/p99.9 latencies would test the cache and predictability hypotheses. Each change
should be measured separately against this baseline.
