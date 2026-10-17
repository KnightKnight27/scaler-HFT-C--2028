# Performance and design report

## Correctness

`./bench --verify` runs the edge-case suite and one million identical
operations against A, B, and C. It compares BBO, bid depth, ask depth, and
aggregated quantities after every operation. The expected output is:

```text
Correctness: PASS
```

## Benchmark results

Run `./bench --impl A`, `./bench --impl B`, and `./bench --impl C` on the
submission machine and record the five-run medians below. The program prints
the measured values and a final checksum; values are intentionally not
fabricated because hardware and OS scheduling materially affect latency.

| Metric | A: ordered maps | B: lazy heaps | C: indexed arrays |
|---|---:|---:|---:|
| Total time (ms) | 157.410 | 128.155 | **104.057** |
| Average ns/add | 111.425 | 71.558 | **60.280** |
| Average ns/cancel | 291.444 | 224.549 | **213.116** |
| Average ns/BBO | 14.849 | 16.581 | **14.320** |

These measurements were collected on macOS using the repository's C++17
`-O2` build. They are representative local results, not portable performance
claims. All three runs produced the same checksum:

```text
99.990000 x 3003 | 100.010000 x 2394|10|10
```

## Why the designs differ

Implementation A pays tree navigation and node allocation costs on level
updates, but its ordering is explicit and its complexity is predictable.
Implementation B makes best-price reads cheap after heap cleanup, while stale
heap entries increase memory use and make depth traversal less cache-friendly.
Implementation C keeps all 10,001 possible ticks in a contiguous array and
caches the best tick. This usually improves locality and makes common adds and
BBO reads simple, but discovering a replacement best level scans the bounded
price band.

The order-ID hash table is shared conceptually by all designs so cancellation
does not require scanning every order. The price-level structures are the
meaningful experimental difference.

## Branches, cache, and page faults

The array design performs predictable contiguous scans and avoids per-level
tree nodes. The map design follows allocator-backed tree pointers, while the
heap design follows heap storage and can retain stale entries. Those access
patterns explain why the latter designs may show more cache misses or branch
activity on a Linux `perf` run. Page-fault counts primarily reflect process
startup, allocator behavior, and the operating system; they are not a direct
measure of algorithmic latency.

On macOS, Linux `perf` counters are unavailable. Instruments Time Profiler,
`sample`, and `time` can provide CPU-stack and wall-clock evidence, but they do
not provide a directly equivalent set of cycles, IPC, branch misses, cache
misses, or dTLB counters. Linux results should therefore be collected on a
Linux host before filling the hardware-counter table.

## Production choice

I would choose the indexed-array design when the instrument's price domain is
fixed as in this brief. It has bounded memory, O(1) BBO access, contiguous
level storage, and simple cancellation lookup. I would still validate the
replacement-level scan against the expected cancellation pattern and consider
a bitmap or block summary if the price range became much larger. The map
design is the most maintainable general-purpose fallback when the price domain
is not bounded; the heap design is attractive when best-price queries dominate
and stale-entry memory is acceptable.

## Further experiments

- Add a bitmap or hierarchical summary to make replacement-best scans faster.
- Compare a flat/open-addressing order-ID table with `unordered_map`.
- Pre-reserve hash-table capacity and measure allocator effects.
- Measure depth-query latency separately from BBO latency.
- Run the same workload at different live-order and price-level densities.
