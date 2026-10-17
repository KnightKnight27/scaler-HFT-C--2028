# Order book results

Archit Kulkarni | 24BCS10194 | 17 October 2026

## Setup and correctness

AMD Ryzen 5 5600H, Ubuntu 26.04 under WSL2, GCC 15.2.0. Built with C++17,
`-O2 -g -fno-omit-frame-pointer`. Collection was pinned to logical CPU 2 using
`taskset -c 2`. The three designs and their complexities are in the README.

Each timed run replayed the same seed-42 workload: 700,746 adds, 199,278 live
cancels and 99,976 BBO queries. Both depth APIs ran another 10,000 times each.
The table uses medians from five fresh-book runs. Workload generation is outside
the timer; per-call clock overhead and depth queries are included in total time.

The verifier passed 20,000 operations with seed 42 and 10,000 with seed 7,
comparing the BBO and **all** price levels after every operation. Boundary tests
also passed. All million-operation benchmark runs ended with checksum
`2736274959807530309`, bid `99.99 x 2826` and ask `100.01 x 2307`.
AddressSanitizer and UndefinedBehaviorSanitizer also passed a 10,000-operation
verification with seed 123. Raw output is in `results/`.

## Measurements

| Metric | A: map | B: sorted vector | C: indexed array |
| --- | ---: | ---: | ---: |
| Total time (ms) | 597.19 | 712.78 | 402.28 |
| ns / add | 496.72 | 399.90 | 258.33 |
| ns / cancel | 881.58 | 1,862.63 | 758.66 |
| ns / BBO | 62.93 | 70.06 | 63.74 |
| IPC | 0.47 | 0.64 | 0.72 |
| Branch misses (%) | 7.79 | 7.23 | 2.65 |
| L1-dcache-load-misses | 86,981,151 | 141,984,238 | 36,401,432 |
| Cache misses | 54,117,551 | 53,633,360 | 26,276,620 |
| Page faults | 16,385 | 16,858 | 28,613 |
| dTLB-load-misses | 6,138,389 | 8,798,171 | 4,828,625 |

Counters are `perf stat -r 5` means per process, with five complete replays per
process, including setup and teardown. Per-call timers were disabled with
`--profile`. All requested counters worked. Hardware events were multiplexed
at about 86% running time; these are perf's scaled counts. IPC is instructions
divided by cycles, and branch-miss percentage is misses divided by branches.

This is a laptop/WSL measurement, with noticeable scheduling and frequency
variation. For example, A's timed totals ranged from 494.53 to 872.48 ms.
The raw repeat values are included rather than treating the medians as exact
hardware limits. Counter totals and timed totals have different scopes.

## What the results show

**Latency and profiles.** C was fastest for adding and cancelling, and about
33% faster overall than A. Its price update is a direct array access, avoiding
a tree walk or binary search. A had the lowest measured BBO average, but its
0.81 ns advantage over C is too small to conclude much given timer overhead
and run variation. All three retrieve the best prices in constant time.

B's add was faster than A's: once the price bands fill, most adds update an
existing level in a compact vector. Its random cancels still search a much
larger order-ID tree. The `perf record -g` reports support this: A spent 41.14%
of sampled cycles in `add_order` and 27.41% in `cancel_order`; B spent 41.44%
in cancellation and another 8.05% in tree erase rebalancing. C spent 37.69%
in cancellation and 12.56% in hash-node insertion. C still pays for ID lookup
and allocation even though its price storage is simple.

**Branches and caches.** B had the most branch misses in absolute terms
(61.24 million), while A had the highest miss percentage. Random tree paths
depend on prices or IDs, so the direction of each comparison is hard to predict.
C avoids the price search and has a much lower miss rate.

A had slightly more generic cache misses than B, but their difference is below
1% and smaller than the observed variability. B clearly had the most L1 misses.
Its price vectors are contiguous, but its order-ID tree contains many separately
allocated nodes, and random cancels follow pointers through those nodes. A also
follows pointers in its price trees and ID hash table. C has contiguous order
slots and a fixed 80,000-byte quantity array, although its hash table still uses
nodes. It recorded the fewest L1, generic cache and dTLB misses. Its higher IPC
is consistent with less time waiting on these accesses; IPC alone is not speed.

**Page faults.** C had the most. Its growing order vector needs new pages and
can copy existing orders during growth, in addition to hash-table allocation.
Recycling slots avoids keeping every historical order, but capacity remains
at its high-water mark. This fits the allocation pattern; the page-fault count
alone does not measure peak memory or prove that disk paging happened.

**Production choice and next steps.** For these fixed price bands I would pick
C. Its level storage is small and bounded, its price updates are simple, and
the measurements show fewer cache misses. Its extra slot/free-list bookkeeping
is manageable. However, hash rehashing, vector growth and scanning up to 5,000
empty prices after best-level removal can still cause latency spikes. None of
these averages establish a tail-latency guarantee.

Next I would reserve capacity for the expected live-order limit, measure peak
resident memory and p99/p99.9 latency, and test sparse books with repeated best
cancellations. An occupancy bitmap could replace the long empty-price scan.
A bounded order pool could reduce allocation spikes. Integer prices are already
used; further changes should be measured before adding complexity.
