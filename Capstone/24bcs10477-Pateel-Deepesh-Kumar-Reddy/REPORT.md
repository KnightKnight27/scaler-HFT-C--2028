# Performance Report

Machine: Intel Core Ultra 5 125H (hybrid P and E cores). Compiler: g++ 16.2.1, `-O2`.
Workload: 1,000,000 ops (70/20/10 add/cancel/BBO), seed 42. Five runs, median.
Counters are from `perf stat -r 5`, using the `cpu_core` row.

| Metric | A (`std::map`) | B (sorted vector) | C (fixed array) |
|---|---|---|---|
| Total time (ms) | 290.09 | 376.51 | 83.16 |
| ns / add | 229.7 | 205.6 | 27.9 |
| ns / cancel | 516.8 | 1050.3 | 186.9 |
| ns / BBO | 32.6 | 31.6 | 29.1 |
| IPC | 0.71 | 0.84 | 1.25 |
| Branch misses (%) | 5.75 | 5.40 | 1.73 |
| L1-dcache-load-misses | 64,107,519 | 100,428,602 | 5,699,248 |
| Cache misses | 31,950,703 | 44,055,242 | 4,581,683 |
| Page faults | 16,353 | 16,827 | 70,394 |

## 11.1 Fastest implementation

C is fastest overall, 83 ms against 290 ms for A and 377 ms for B, so about 3.5x
and 4.5x.

- add: C 27.9 ns, B 205.6, A 229.7. C is about 8x faster. B beats A here: a
  contiguous binary search is cheaper than a red-black descent plus a node
  allocation, even with the vector tail shift.
- cancel: C 186.9, A 516.8, B 1050.3. C is about 3x faster than A and 6x faster
  than B. B is last because cancelling walks its order tree.
- get_bbo: no signal, all three are within timer noise.

C wins because it never searches. With 5,000 ticks per side the level is
`qty[price - base]`, the best price is cached, and the order is a direct array
index. Its cancel advantage is smaller than its add advantage because C alone
pays a linear scan when it empties the cached best level.

## 11.2 Branch prediction

A has the most branch misses (5.75%), then B (5.40%), then C (1.73%), so C has
roughly 3x fewer. A descends a red-black tree on every operation. A node's left
and right children look the same to the branch predictor, and which one is
populated depends on the whole insertion history, so the outcome is close to a
coin flip each time.

The interesting part is that B is nearly as unpredictable as A but much slower.
B's binary search re-visits the same array positions, which is comparatively
easy to learn. So B's problem is memory, not misprediction. A lower branch-miss
rate does not by itself mean a faster program.

## 11.3 Cache behavior

B has the most misses (100.4M L1d, 44.1M cache), against 64.1M / 32.0M for A and
5.7M / 4.6M for C.

- B keeps orders in a `std::map` of hundreds of thousands of individually
  allocated nodes, so every cancel walks about twenty pointer-chasing levels and
  adds rebalance. `perf record` shows `_Rb_tree_rebalance_for_erase` and the
  binary search as its hot spots. Its level vector also shifts up to 5,000
  records per insert.
- A also uses a node-based tree for levels, but only up to 5,000 levels exist at
  once, so that tree is small. Its order store is a hash map, which is friendlier
  to the cache than a tree. That is why A is below B.
- C is two flat 40 KB arrays plus a flat order vector. The memory is sequential,
  one cache line covers several levels, and levels are found by arithmetic
  instead of following pointers. Its dTLB misses (0.29M) are about 10x lower
  than A and B.

Main point: A and B differ most in their order store (hash against tree), and
that is where the cache-miss gap comes from. A tree keyed by order id is the
worst structure here, because it is both cache-hostile and pointer-chasing on
the hottest path.

## 11.4 Page faults

C causes about 4x more page faults (70,394 against 16,353 and 16,827). C reserves
`2^21 + 1` order slots up front, roughly 50 MB, while A and B hold only the live
orders at any time. Here page faults track memory footprint, not speed: C pays
the cost once at construction and still finishes well ahead of both.

## 11.5 Production choice

I would take C's level array and replace its order store. It wins on latency and
on predictability: its cost does not depend on book depth or on the price
distribution, whereas A depends on tree balance and B on where an insert lands.
Its worst case is a bounded, rare walk of 5,000 slots. Its cache behavior is
flat and contiguous, which per 11.3 is what actually matters.

Its one weakness is memory: a fixed 50 MB. I would swap the id-indexed array for
an open-addressing table sized to the expected number of live orders. That is one
probe per cancel, which is where C's lead is smallest, and it removes both the
50 MB and the id-range limit. C is also the simplest to maintain, with no
rebalancing and no sorted invariant to keep. B is the cautionary case: a
`std::map` keyed by order id looks tidy but produces the slowest and most
cache-hostile design, because of that one choice.

## 11.6 What I would try next

- Replace C's id-indexed order array with an open-addressing table, to remove the
  50 MB and the id-range limit.
- Replace C's best-level scan with a bitset of non-empty levels plus
  count-trailing-zeros, making cancel O(1) unconditionally.
- Benchmark B with a hash order store, to isolate the tree-against-hash cost that
  11.3 blames.
- Report p99 as well as the median, since B's tree walk should make its tail
  sensitive to the input.
- Pin the CPU and disable turbo, because this machine drifts by around 10%,
  which is larger than several of the gaps measured.

Integer ticks, fixed arrays and pre-allocation are already used, so they are not
listed as future work.
