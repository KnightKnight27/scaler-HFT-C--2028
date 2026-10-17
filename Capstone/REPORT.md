# Order book results

Archit Kulkarni | 24BCS10194 | 17 October 2026

## Setup

Ryzen 5 5600H, Ubuntu 26.04/WSL2, GCC 15.2.0, C++17,
`-O2 -g -fno-omit-frame-pointer`, pinned to logical CPU 2.
Each run used seed 42: 700,746 adds, 199,278 live cancels, 99,976 BBO queries
and 10,000 depth queries per side. The table uses five-run medians.

Verification passed 20,000 operations with seed 42 and 10,000 with seed 7,
checking every level after each operation. AddressSanitizer and
UndefinedBehaviorSanitizer passed 10,000 operations with seed 123.
All million-operation benchmarks produced checksum `2736274959807530309`
and final BBO `99.99 x 2826 / 100.01 x 2307`. Raw output is in `results/`.

## Results

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

API times include clock overhead; total time also includes depth queries and
checksum handling, but excludes generation and book construction/destruction.
Counters are `perf stat -r 5` means for the whole process: five book replays
with per-call timers disabled, plus setup and teardown. All requested counters
worked. Hardware events ran about 86% of the time; counts are perf's scaled
values. IPC = instructions/cycles; branch miss rate = misses/branches.

This laptop had noticeable timing variation: A ranged from 494.53 to 872.48 ms.
The medians should be read with that limitation. Timed totals and perf counters
cover different amounts of work.

## Analysis

**Latency.** C was fastest for add and cancel, and about 33% faster overall
than A. It updates a price by indexing an array. B's compact price vectors
made adds faster than A's tree searches once most levels were occupied, but
random cancellation still searches its larger order-ID tree. A's measured
BBO time beat C by only 0.81 ns, too small to rank confidently with these
timers. All three find the best prices in O(1).

**Profiles.** `perf record -g` put A's add/cancel at 41.14%/27.41% of sampled
cycles. B spent 41.44% in cancellation and 8.05% in tree erase rebalancing.
C spent 37.69% in cancellation and 12.56% in hash-node insertion: array price
storage removes a search, but ID lookup and allocation still cost time.

**Branches and caches.** B had the most branch misses (61.24 million); A had
the highest miss rate. Random comparisons make tree paths hard to predict.
C removes the price search and had the lowest rate. A's generic cache misses
were slightly higher than B's, but the difference was below 1% and smaller
than run variation. B clearly had the most L1 misses: its levels are compact,
but its ID tree follows pointers through separately allocated nodes. A also
uses tree and hash nodes. C's order slots and 80,000-byte level arrays are
contiguous, although its ID hash table still uses nodes. It recorded the
fewest L1, generic cache and dTLB misses, consistent with its higher IPC.

**Page faults.** C had the most. Its order vector grows and copies into new
storage, alongside hash-table allocation. Cancelled slots are reused, but
capacity stays at its peak. This fits its allocation pattern; page faults
alone do not establish peak memory use or disk paging.

**Production choice.** I would choose C for these fixed bands: small bounded
level storage, fast updates and fewer cache misses. The slot/free-list code
is manageable. Its weak points are allocation/rehash spikes and a possible
5,000-tick scan when the best level empties. Average times do not establish
tail latency or production readiness.

**Next steps.** Reserve order capacity, measure peak memory and p99/p99.9,
and test sparse books with repeated best-level cancellations. An occupancy
bitmap could shorten the scan, and a bounded pool could reduce allocations.
Integer prices are already used.
