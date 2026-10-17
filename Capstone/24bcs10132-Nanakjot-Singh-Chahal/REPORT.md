# Performance Analysis

Nanakjot Singh Chahal, 24BCS10132

All numbers come from my machine (i9-14900HX, WSL2, g++ 13.3 `-O2`). The results table is
in [README.md](README.md#performance-results). WSL2 has no hardware PMU, so branch and
cache figures come from Valgrind cachegrind (a simulation). `perf` provided task-clock,
page-faults and `perf record` (cpu-clock sampling).

One property of the workload shapes everything below. Prices are uniform over 5,000 ticks
per side and ~500k orders end up resting, so **every price level is occupied within the
first few thousand ops**. After that, adds almost never create a level and cancels almost
never empty one. The level structures stay a fixed size (5,000 entries per side). The
**order-id structure** grows to 500k entries (≈ 16–32 MB), far bigger than L2. The cost of
each design is mostly the cost of its id lookup.

## 11.1 Which implementation is fastest?

| | fastest | why |
|---|---|---|
| add_order | C (116 ns), with B close (122 ns) | C writes one 32-byte slot into a flat table and bumps an array cell, with no allocation. B inserts ids into a `std::map`. Ids increase monotonically, so every insert walks the same right-most path of the tree, which stays hot in cache. A pays for a `malloc`'d hash node plus occasional full rehashes (`_M_rehash` is 4.5% of A's samples). It also does a `std::map` descent over 5,000 levels with a random price at every add. |
| cancel_order | **C (114 ns)**, vs A 262 ns and B 442 ns | Cancels pick a *random* live order, so every design takes at least one cache miss to find it. C takes about one miss: hash, then the slot holds tick/side/qty inline. A takes a bucket load and a node load (two dependent misses), then `std::map::find` over the levels. B does a ~19-level pointer chase through a 500k-node red-black tree, then `_Rb_tree_rebalance_for_erase` (6% of B's samples). |
| get_bbo | a tie (35–42 ns, ~16 ns of which is the timer) | All three are O(1): `begin()`, heap `top()`, or a cached index. The best levels are always in L1. |
| whole run | **C, 42 ms vs 165–175 ms (≈4×)** | C executes 2× fewer instructions than A, 3.5× fewer than B, and has 5–7× fewer L1 misses. |

The per-op timings show something interesting. C's *uninstrumented* run averages 42 ns
per op, yet its timed add is 116 ns. `clock_gettime` acts as a barrier, so with timers in
place each op's cache miss is paid in full. Without timers the out-of-order core overlaps
the miss of one op with the work of the next (memory-level parallelism). A and B gain less
from this because their misses are *dependent*: a pointer chase can't start the next load
until the previous one returns.

## 11.2 Branch prediction

**A has the most mispredictions**: 18.9 M, a 7.5% rate, against B's 1.6% and C's 3.1%.
Each `std::map` lookup on a random price is a ~13-level descent where the decision
`key < node->key` is effectively a coin flip, so the predictor can't learn it. A does this
on every add *and* every cancel. B's absolute count is similar (18 M), but it executes
1.1 G branches, mostly in heap sift-down while copying and popping heaps in `get_bids`.
Those comparisons are fairly regular, which gives the low rate. Its id-tree descents are
predictable because ids are monotonic. C has very few branches at all (80 M). Its remaining
misses come from the linear-probe loop ("is this slot mine?") and the `switch` on the op
type in the benchmark driver, which is random by design and is shared by all three.

## 11.3 Cache behaviour

| | contiguous? | pointer chasing? | many small allocations? |
|---|---|---|---|
| A | no | yes: tree nodes and hash-chain nodes | yes: 1 hash node per order, 1 tree node per level |
| B | heap vector yes, everything else no | yes: 500k-node id tree, hash nodes | yes: 1 tree node per order |
| C | **yes**: `qty[10001]` (80 KB) and bitmap (1.2 KB) fit in L2, and the id table is one flat array | no | none (one 32 MB allocation per book) |

**B has the most L1 misses** (35 M), from the large id tree and from copying a 5,000-entry
heap (20 KB) for every depth query. A is next (24 M). **C has by far the fewest L1 misses**
(4.9 M), but the **most last-level misses** (3.3 M vs ~2.5 M). Its 32 MB `OrderMap` is
indexed by a Fibonacci hash, which deliberately scatters consecutive ids across the whole
table, so each add or cancel touches a random line that is usually not in any cache. A and
B allocate their nodes in roughly arrival order, so consecutive adds land on neighbouring
heap memory. An experiment confirmed this: switching C to an identity hash (`id & mask`)
made consecutive adds write consecutive slots, and **add fell from ~110 ns to 31 ns** with
an identical checksum (`results/experiment_identity_hash.txt`). C still wins overall because
it pays roughly one miss per operation, where A and B pay a chain of dependent misses.

## 11.4 Page faults

**C causes the most page faults: 90,868 vs ~16,500 for A and B.** This comes from its
allocation pattern, not from using more memory overall. Each `ArrayOrderBook` allocates a
1M-slot × 32 B = 32 MB table up front and writes every slot to mark it empty. That is 8,192
pages per book. The benchmark builds 10 books per process (5 runs × 2 passes), and each
32 MB block is returned to the OS on destruction (glibc uses `mmap` for large blocks), so
the next book faults it all in again: 10 × 8,192 ≈ 82k, plus the shared ~9k baseline for
the 32 MB workload vector. A and B allocate millions of small nodes from the malloc heap.
When a book is destroyed those chunks go onto free lists and are reused by the next run, so
they fault only once. In production C would allocate once at startup and never fault on the
hot path. For A and B, the page faults only *look* cheap. The real cost is `malloc`/`free`
on every add and cancel (visible in their profiles), with no cap on latency.

## 11.5 Production choice

**I would deploy C** (with the improvements in 11.6). The decision isn't based on its mean
alone:

* **Latency:** best or tied-best on every operation, and ~4× faster end to end.
* **Predictability:** there is no allocation on the hot path (A and B `malloc` on every add
  and `free` on every cancel), no rehash pauses (A's `unordered_map` rehash copies every
  node at once), and no rebalancing. The worst case for finding a new best price is a
  157-word bitmap scan, and that is bounded.
* **Memory:** fixed and known up front (80 KB of levels and a sized id table). A and B grow
  without limit and fragment the heap. The cost is that C must be sized for peak orders, and
  its `grow()` is a pause, so it should be pre-sized to make that never happen.
* **Cache:** flat arrays, no pointer chasing, and the hot level data fits in L1/L2.
* **Complexity / maintainability:** this is C's weak point. It relies on a hand-written hash
  table (backward-shift deletion is subtle) and on a fixed price band known at compile time.
  A is ~70 lines of obvious standard-library code. I would mitigate this with the lockstep
  `--verify` test against A, which stays as the reference implementation in CI. B is the one
  I would not ship: it has the slowest cancels and an O(L) depth query.

## 11.6 What I would try next

1. **Locality-preserving id lookup.** Ids from one gateway are usually monotonic, so use a
   ring buffer or a direct `vector` indexed by `id - base`, or keep the open-addressing table
   with a cheap hash (`id & mask`). Measured: add 110 → 31 ns.
2. **Pre-fault and huge pages.** Allocate the id table once at startup, touch it, and use
   `madvise(MADV_HUGEPAGE)`. That removes the run-time page faults and most dTLB misses on
   a 32 MB table.
3. **Smaller slots.** Pack `OrderInfo` into 16 B (an int16 tick offset, 1 byte for side and
   used flag, an int32 qty) so 4 slots fit in a cache line instead of 2.
4. **Integer API.** Take ticks directly instead of `double`. `llround` is ~2% of C's
   samples, and the off-grid check would disappear.
5. **Branch reduction** in the probe loop (compare id and used flag in one test), and a
   two-level bitmap (a summary word over the 157 words) so the scan for a new best price is
   just two `clz` operations.
6. **Per-order queues** (an intrusive FIFO list per level, from a pool) if price-time
   priority or matching is needed later. The pool keeps them allocation-free.
7. Re-run `scripts/run_perf.sh` on bare-metal Linux to confirm the cachegrind conclusions
   with real `cycles`, `instructions` (IPC) and `dTLB-load-misses`.
