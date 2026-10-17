# Performance Report

Vansh Dobhal - 24BCS10099

Machine: Intel i5-13450HX, Linux 6.6, g++ 16.2 -O2, pinned to one P-core (`taskset -c 0`).
Raw outputs are in results/.

## Results table

Timings are the median of 5 runs from `./bench --impl X`. Counters are from
`perf stat -r 5 ... ./bench --impl X --runs 1` (cpu_core counters).

| Metric | Implementation A (map) | Implementation B (vector) | Implementation C (array) |
|---|---|---|---|
| Total time (ms) | 165.57 | 218.83 | 52.09 |
| ns / add | 140.7 | 147.5 | 32.6 |
| ns / cancel | 238.5 | 472.0 | 51.9 |
| ns / BBO | 13.1 | 12.8 | 12.7 |
| IPC | 0.90 | 1.03 | 1.43 |
| Branch misses (%) | 5.20% (8.82 M) | 4.56% (12.43 M) | 2.02% (1.59 M) |
| L1-dcache-load-misses | 13.54 M | 18.61 M | 1.59 M |
| Cache misses | 4.00 M | 4.94 M | 1.93 M |
| Page faults | 15,618 | 14,333 | 12,022 |

Two things to keep in mind when reading this table:

1. **perf stat counts the whole program**, including building the 1M op workload.
   I measured that part alone with `./bench --impl none` (results/perf_stat_baseline.txt):
   26.9 ms, 214 M instructions, 0.81 M branch misses, 0.48 M L1 misses, 0.77 M cache misses,
   8,226 page faults. That cost is the same for all three, so the differences between
   A, B and C come from the books themselves.
2. **Timer overhead.** Each operation is timed with two `steady_clock::now()` calls. I measured an
   empty pair of calls at about 12.9 ns on this machine. So ns/BBO (~13 ns for all three) is basically
   just the timer, get_bbo itself is too fast to measure this way. The add/cancel numbers also
   contain this ~13 ns.

## 11.1 Which implementation is fastest?

C is fastest for everything:

- add: C 32.6 ns vs A 140.7 vs B 147.5 (about 4x faster)
- cancel: C 51.9 ns vs A 238.5 vs B 472.0 (4.6x faster than A, 9x faster than B)
- get_bbo: all three are O(1) and all hit the timer floor (~13 ns), so it is basically a tie.
  A reads `begin()` of a map (A keeps a pointer to the leftmost node), B reads `back()` of a vector,
  C reads two cached ints plus one array slot.

Why C wins: an add is `orders[id] = ...`, `qty_at[tick] += qty` and one compare for the best price.
A cancel is one load of `orders[id]` and one subtract. No allocation, no tree walk, no hashing,
no searching. It also runs the fewest instructions: after taking away the 214 M baseline, C uses about
295 M instructions, compared to 686 M for A and 1102 M for B.

From perf record, A spends its time in `MapBook::cancel_order` (21.7%), `std::map::operator[]`
(13.2%, finding/creating the level on add) and the unordered_map insert/lookup (about 10%).
B spends it in `VectorBook::cancel_order` (21.1%), `lower_bound` on the level vectors
(about 17% in two copies) and red black tree rebalancing (`_Rb_tree_rebalance_for_erase` 7.1%, insert 2.1%).
For C, the biggest symbols are `clock_gettime` (29.5%), the bench loop with the book code inlined
into it (20.6%) and `make_workload` (15.4%). So in C the book is now cheaper than the timing and the
workload generation around it.

B's add is close to A's because ids come in increasing order, so the std::map insert always goes
down the right edge of the tree which stays in cache. But B's cancel picks a random old id, so it walks
about 19 levels of a tree with ~500k nodes spread around the heap, and then also has to rebalance.
That is why B's cancel is twice as slow as A's hash lookup.

## 11.2 Branch Prediction

B has the most branch misses in total (12.4 M), A has the highest miss rate (5.2%).

- **B:** `lower_bound` over up to 5000 levels is about 12-13 compares, and whether you go left or right
  depends on random prices, so each compare is basically a coin flip for the predictor.
  The id lookup in `std::map` is the same story, about 19 more data dependent left/right decisions per cancel.
- **A:** both the std::map level lookup and the hash table have unpredictable branches
  (tree direction, hash bucket chain check), and the tree rebalancing code on insert/erase
  has many branches that depend on node colours.
- **C:** almost no data dependent branches. The `tick > best_bid` check is nearly always false
  since the best is already near the mid price, and the "walk to next level" loop runs very rarely
  because levels near the top have thousands of quantity and almost never become empty.
  Most of C's 1.59 M misses are actually from the workload generator (0.81 M baseline) and the timing loop.

## 11.3 Cache Behavior

B has the most cache misses (4.94 M) and L1 misses (18.6 M), then A (4.00 M / 13.5 M), then C (1.93 M / 1.59 M).

- **A** follows pointers. Every std::map level is a separate heap node and every order is a separate
  hash node. A cancel is: hash bucket -> node (likely a miss since the order is old and random),
  then a tree walk through the level map. The level map has at most 10k nodes (~0.5 MB) so it partly stays in L2.
- **B** also follows pointers for the ids, and the id tree is the big one (~500k nodes, tens of MB),
  so a random cancel jumps around memory at almost every level of the tree. The level vectors are
  contiguous (good), but binary search still touches positions far apart in the array.
  It is also the one with the most dTLB misses (0.90 M), because the tree nodes are spread over many pages.
- **C** is mostly contiguous. `qty_at` is 117 KB and fits in L2, and the levels near the best price
  (where most activity is) stay in L1. The orders vector is big (one 16 byte slot per id) and a cancel
  is a random access into it, so that is about one miss per cancel, but it is one miss, not a chain of
  dependent misses like a tree walk. C does not allocate anything per order.

This matters because a dependent pointer chase cannot be overlapped: the CPU has to wait for one node
before it knows the address of the next one. That shows in IPC: A 0.90 and B 1.03 (lots of stalled
cycles) vs C 1.43.

## 11.4 Page Faults

A has the most page faults (15.6k), then B (14.3k), then C (12.0k). Subtracting the 8.2k baseline
(the workload vector and program start), the books themselves cause about 7.4k (A), 6.1k (B) and 3.8k (C).

- A allocates a heap node for every order and every level, and the unordered_map also has to rehash
  as it grows, allocating a bigger bucket array each time. So it touches the most new memory.
- B allocates one tree node per order plus the two level vectors.
- C has no per order allocations. Its memory is the fixed 117 KB array plus one big vector that grows
  a few times (`resize(id * 2 + 16)`). The faults it does have come from that vector growing
  (`clear_page_erms` and `_M_default_append` show up in its perf report).

So page faults here mostly track how much fresh memory each design touches. In a real system I would
pre-allocate (and touch) that memory at startup so no page faults happen while trading.

## 11.5 Production Choice

I would deploy **C**, with some changes, and not just because it has the lowest average.

- **Latency:** fastest on add and cancel by ~4x, and BBO is two cached ints.
- **Predictability:** no malloc/free on the hot path, no rehash pauses (A can stall for a whole rehash
  of a big table), no tree rebalancing. Each operation does about the same small amount of work every time.
  The one bad case is cancelling the last order at the best level when the next level is far away, which
  means walking the array. With prices limited to 5000 ticks per side that walk is bounded, and in a normal
  book the next level is usually 1-2 ticks away.
- **Memory:** levels are fixed at 117 KB. The order vector is the weak part: it grows with the
  largest id and never shrinks, and it would be huge if ids were sparse. In production I would replace it
  with a pre-allocated pool of order slots plus an id -> slot map (or a ring indexed by id modulo pool size
  if ids are sequential).
- **Cache behaviour:** best of the three by a wide margin (8x fewer L1 misses than A).
- **Complexity / maintainability:** it is actually the shortest of the three. The tricky parts are only
  the best price rescan and the fact that it depends on the price band being small and fixed. If the price
  range was much bigger or unknown, A (std::map) would be the safer choice because it works for any price,
  is easy to read and is hard to get wrong.

B is the one I would not use: it is slowest and its worst case (inserting a new level at the far end of a
vector) shifts the whole vector.

## 11.6 What Would I Try Next?

- **Pre-allocate the order storage** (object pool / fixed vector sized at startup) and touch it once
  so there are no page faults or resizes while running.
- **Better id lookup for sparse ids:** an open addressing hash map with a flat array instead of
  std::unordered_map nodes, so a lookup is one cache line most of the time.
- **Bitmap of non empty levels:** keep one bit per tick, then finding the next best level after a cancel
  is a few `__builtin_ctzll` calls over 64-bit words instead of walking the array. Also makes get_bids
  O(d) even when the book is sparse.
- **Smaller structs:** `int` qty instead of `long`, and packing tick + qty in 8 bytes, so more orders
  fit in a cache line.
- **Time in batches** instead of every single op (or use `rdtsc`) so the ~13 ns timer cost does not hide
  the real cost of get_bbo.
- **Keep per order FIFO queues** at each level (intrusive linked list inside the pool) if I wanted
  to extend this into a matching engine later.
