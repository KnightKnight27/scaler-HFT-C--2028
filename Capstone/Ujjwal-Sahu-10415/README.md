# Capstone: Low-Latency Order Book

### Name: Ujjwal Sahu
### Roll No: 24BCS10415
### SST Email: <ujjwal.24bcs10415@sst.scaler.com>

## 1. Interface Changes

The public `OrderBook` API (`Side`, `Level`, `BBO`, `add_order` /
`cancel_order` / `get_bbo` / `get_bids` / `get_asks`) is unchanged from the
required interface, with one internal change:

- **Prices are converted to integer ticks internally** (`price_ticks = round(price
  * 100)`, in `Common.h`). Since the tick size is fixed at 0.01, every valid
  price maps to a unique integer, so all three implementations store and
  compare ticks internally, never raw `double`s, and only convert back to
  `double` at the API boundary (`get_bbo`, `get_bids`, `get_asks`). This
  avoids float comparison/hashing bugs in the hot path. The public function
  signatures are unchanged (`double price` in, `double price` out).

No other interface changes. There is no order matching, as required.

## 2. How to Build

Requires `g++` with C++17 support.

```
make            # builds both ./demo and ./bench
make demo       # just the sanity-check demo (prints behavior of A/B/C side by side)
make bench      # just the benchmark/verify binary
```

Equivalent single commands:

```
g++ -std=c++17 -O2 -Wall -Wextra main.cpp OrderBookA.cpp OrderBookB.cpp OrderBookC.cpp -o demo
g++ -std=c++17 -O2 -Wall -Wextra bench.cpp OrderBookA.cpp OrderBookB.cpp OrderBookC.cpp -o bench
```

## 3. How to Run the Correctness Test

```
./bench --verify
```

Runs 1,000,000 identical operations (fixed seed `mt19937(42)`) through all
three implementations in lockstep, diffing `get_bbo()` after every operation
and `get_bids(10)`/`get_asks(10)` at the same 100-op checkpoints the workload
defines. Implementation A is used as the reference.

## 4. How to Run the Benchmark

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Each prints the median of 5 runs over the same 1,000,000-op workload
(70% add / 20% cancel / 10% BBO, `get_bids(10)`/`get_asks(10)` every 100 ops),
plus a final BBO and checksum to prevent the compiler from optimizing the
work away.

## 5. How to Run the perf Measurements

perf is Linux-only. On a Linux machine with `perf` installed:

```
./run_perf.sh
```

This builds `bench`, runs `perf stat -r 5` with all 9 counters and `perf
record -g` / `perf report` for A, B and C, and runs `--verify`, saving
everything to `perf_results/`.

**Note on this machine's perf.data call graphs:** `-O2` omits frame
pointers, so `perf record -g`'s default (frame-pointer-based) stack walker
produces garbage symbol names a few levels deep in B's and C's call trees
(e.g. raw hex addresses). The top-level self-time percentages per function
are unaffected (they come straight from the sampled instruction pointer) and
are what the analysis below relies on. A deeper, trustworthy call graph would
need `perf record -g --call-graph=dwarf`.

## 6. Data Structure Choices

### Implementation A — `std::map` + `std::list`

- **Per-order storage:** `unordered_map<id, OrderLocation>`, where
  `OrderLocation` holds the side, price (ticks), and an iterator directly
  into the price level's list node.
- **Per-price storage:** `std::map<ticks, PriceLevel>` (descending comparator
  for bids, ascending for asks). Each `PriceLevel` holds a running total
  quantity and a `std::list<long>` of individual order quantities.
- **Best bid/ask:** falls out of the map's own structure — `begin()` is
  always the best price (libstdc++ caches the leftmost tree node, so this is
  O(1), not a search).
- **Cancellation:** `unordered_map` gives the order's location in O(1)
  average; the list iterator lets the order be unlinked in O(1) without
  scanning the level.

| Operation | Complexity |
|---|---|
| add_order | O(log L) — L = distinct price levels on that side |
| cancel_order | O(log L) worst case (only when a level empties and must be erased from the map); O(1) avg otherwise |
| get_bbo | O(1) |
| get_bids / get_asks | O(depth) |

### Implementation B — `unordered_map` + `std::set`

- **Per-order storage:** `unordered_map<id, OrderInfo{side, ticks, qty}>` —
  the single source of truth, no secondary per-level list.
- **Per-price storage:** `unordered_map<ticks, qty>` per side — O(1) average
  aggregate updates, no tree.
- **Best bid/ask:** a separate `std::set<ticks>` per side tracks which
  levels are currently non-empty. Best bid = `*rbegin()`, best ask =
  `*begin()`. This set is only touched when a level opens or fully empties,
  not on every add/cancel.
- **Cancellation:** the order map gives side/price/qty directly in O(1)
  average; no scan.

| Operation | Complexity |
|---|---|
| add_order | O(1) avg; O(log L) only when a new price level opens |
| cancel_order | O(1) avg; O(log L) only when a level empties |
| get_bbo | O(1) |
| get_bids / get_asks | O(depth) |

### Implementation C — flat array (own design)

Built around the fact the whole instrument only has ~10,000 valid price
levels in total.

- **Per-price storage:** one flat `std::array<long, 10001>`, indexed
  directly by `ticks - kMinTick`. No hashing, no tree, no pointers.
- **Per-order storage:** `std::vector<OrderSlot>` indexed directly by order
  id (grown with manual doubling, not `resize(id+1)`, to stay amortized
  O(1) instead of an O(n²) growth trap). This assumes ids are reasonably
  dense/small — true for the benchmark's sequential id generator; a sparse
  id space would need a hybrid (e.g. an id→slot hash map) instead.
- **Best bid/ask:** two plain ints. Updated in O(1) when a *new* level opens
  at a better price. Only rescanned — a straight-line walk over contiguous
  memory — when the level that *was* best goes to zero.
- **Cancellation:** order id directly indexes the order table, O(1), no scan.

| Operation | Complexity |
|---|---|
| add_order | O(1) amortized |
| cancel_order | O(1) typical; O(k) worst case only when the best level is removed (k = consecutive empty ticks scanned to find the next best) |
| get_bbo | O(1) |
| get_bids / get_asks | O(depth) typical; O(ticks scanned) worst case on a sparse book, but a contiguous-memory scan stays fast even then |

## 7. Correctness Results

```
Running correctness test...

Implementation A: PASS (reference)
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## 8. Performance Results

All numbers below are from the author's own Linux machine (`./run_perf.sh`),
per the assignment's rules on using your own environment. All 9 requested
counters were available (no `<not supported>` events on this machine).

### Application-level timing (`./bench --impl <X>`, median of 5 runs, 1,000,000 ops)

| | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 165.90 | 85.04 | 40.21 |
| ns / add | 129.33 | 58.12 | 21.47 |
| ns / cancel | 279.60 | 120.78 | 28.97 |
| ns / BBO | 17.97 | 18.55 | 16.26 |

### `perf stat -r 5` output

**Implementation A**
```
 Performance counter stats for './bench --impl A' (5 runs):

          2,933.51 msec task-clock                                                              ( +-  1.63% )
     7,264,502,923      cycles                                                                  ( +-  1.61% )  (71.41%)
     4,920,506,134      instructions                                                            ( +-  0.04% )  (85.71%)
     1,022,265,985      branches                                                                ( +-  0.04% )  (85.72%)
        41,019,014      branch-misses                                                           ( +-  0.06% )  (85.68%)
       113,675,323      cache-misses                                                            ( +-  2.39% )  (85.71%)
       135,349,158      L1-dcache-load-misses                                                   ( +-  0.07% )  (85.74%)
        11,751,443      dTLB-load-misses                                                        ( +-  0.35% )  (57.15%)
            26,590      page-faults                                                             ( +-  0.00% )

       2.942562021 +- 0.049178758 seconds time elapsed  ( +-  1.67% )
```

**Implementation B**
```
 Performance counter stats for './bench --impl B' (5 runs):

          1,526.57 msec task-clock                                                              ( +-  1.39% )
     3,783,926,312      cycles                                                                  ( +-  1.37% )  (71.43%)
     3,584,219,817      instructions                                                            ( +-  0.15% )  (85.71%)
       684,926,588      branches                                                                ( +-  0.15% )  (85.71%)
         8,092,876      branch-misses                                                           ( +-  0.16% )  (85.72%)
        41,253,211      cache-misses                                                            ( +-  3.75% )  (85.71%)
        58,025,753      L1-dcache-load-misses                                                   ( +-  0.25% )  (85.73%)
         3,659,720      dTLB-load-misses                                                        ( +-  0.38% )  (57.13%)
            38,005      page-faults                                                             ( +-  0.00% )

       1.530014069 +- 0.021337671 seconds time elapsed  ( +-  1.39% )
```

**Implementation C**
```
 Performance counter stats for './bench --impl C' (5 runs):

            649.18 msec task-clock                                                              ( +-  1.01% )
     1,608,526,371      cycles                                                                  ( +-  1.00% )  (71.32%)
     2,304,440,128      instructions                                                            ( +-  0.15% )  (85.66%)
       405,047,880      branches                                                                ( +-  0.21% )  (85.72%)
         2,801,744      branch-misses                                                           ( +-  1.47% )  (85.84%)
        19,047,232      cache-misses                                                            ( +-  2.00% )  (85.82%)
        20,206,175      L1-dcache-load-misses                                                   ( +-  0.76% )  (85.52%)
           500,424      dTLB-load-misses                                                        ( +-  1.33% )  (57.10%)
            73,663      page-faults                                                             ( +-  0.00% )

       0.650469744 +- 0.006576026 seconds time elapsed  ( +-  1.01% )
```

(Note: `perf stat -r 5` reruns the whole `bench` process 5 times, and each
process internally repeats the full 1,000,000-op workload 5 more times for
its own median — so each `time elapsed` above covers 25 full passes. The
application-level table further up, from `./bench --impl <X>` run directly,
is the cleaner per-pass number.)

### Results Table

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 165.90 | 85.04 | 40.21 |
| ns / add | 129.33 | 58.12 | 21.47 |
| ns / cancel | 279.60 | 120.78 | 28.97 |
| ns / BBO | 17.97 | 18.55 | 16.26 |
| IPC | 0.677 | 0.947 | 1.433 |
| Branch misses (%) | 4.01% | 1.18% | 0.69% |
| L1-dcache-load-misses | 135,349,158 | 58,025,753 | 20,206,175 |
| Cache misses | 113,675,323 | 41,253,211 | 19,047,232 |
| Page faults | 26,590 | 38,005 | 73,663 |

## 9. Performance Analysis

### 9.1 Which implementation is fastest?

**C is fastest on every operation** — `add_order` (21.5 ns vs 58 ns for B and
129 ns for A), `cancel_order` (29 ns vs 121 ns and 280 ns), and `get_bbo`
(16.3 ns, only marginally ahead of A and B's ~18 ns, since all three reduce
`get_bbo` to a couple of cached reads). B is consistently in between, A is
slowest on every metric.

The IPC numbers explain why directly: A executes 0.68 instructions per
cycle, B 0.95, C 1.43. A isn't doing more *useful* work than C — the
`perf stat` instruction counts show A actually executes *more* total
instructions (4.9B) than C (2.3B) for the identical 1,000,000-op workload,
because every `std::map` operation does O(log L) tree-comparison work and
every cancel walks through `std::list` node (de)allocation. C does a single
array index arithmetic per operation. Fewer instructions, and each one
retires faster because the CPU isn't stalled waiting on cache misses or
mispredicted branches (see below) — that's the whole story.

### 9.2 Branch Prediction

**A has by far the most branch misses: 4.01%, vs 1.18% for B and 0.69% for
C.** A also executes nearly 50% more branch instructions in total
(1.02B vs 685M for B, 405M for C) for the same workload.

The cause is structural: every `std::map` lookup/insert/erase walks a
red-black tree, and at every node the CPU has to branch on "is this price
greater or less than the target" — and since the workload's prices are
uniformly random across the band, that comparison outcome is essentially a
coin flip the branch predictor can't learn. Worse, every node visited also
needs a rebalancing check (rotate or not) on insert/erase, another
data-dependent branch. B still has some branches from hash-bucket/collision
handling, but no multi-level, data-dependent descent. C's only branches are
simple, highly predictable bounds checks (`i >= kBidLowIndex`) and the
occasional "is this a new best price" check — nothing that depends on the
random price value itself in an unpredictable way.

### 9.3 Cache Behavior

**A has by far the most cache misses (113.7M) and L1-dcache-load-misses
(135.3M)**, roughly 3x B's and 6-7x C's.

A's `PriceLevel` and list nodes are individually heap-allocated and scattered
across the heap — walking the tree and the per-level list means following
pointers to wherever `malloc` happened to place each node, which is rarely
the next cache line. The `perf report` output confirms this isn't just
theory: **~10% of A's total cycles are spent inside glibc's `malloc`/`free`
machinery** (`unlink_chunk`, `_int_free_merge_chunk`, `_int_free_chunk`) —
the cost of constantly allocating and freeing small tree/list nodes on every
add and cancel. B still heap-allocates hash table nodes, but has no
tree-depth of pointer hops per operation, just one hash + one bucket walk.
C never allocates during a normal add/cancel at all (the array is sized
once, the order vector grows only occasionally via doubling) and only ever
touches 1-2 contiguous array slots per operation — the most cache-friendly
access pattern possible.

### 9.4 Page Faults

This is the one metric where **C is actually the worst** (73,663 page
faults, vs 38,005 for B and only 26,590 for A) — a genuinely counterintuitive
result that's worth explaining rather than hiding.

The cause is C's order-id-indexed `std::vector<OrderSlot>`, grown by
doubling (1 → 2 → 4 → ... → >1,000,000 slots) as larger order ids arrive.
Each time it grows, `std::vector::resize` allocates an entirely new,
larger contiguous block and default-constructs every new slot — meaning
*the OS must back a large span of freshly-touched virtual memory with
physical pages all at once*, and the vector's own capacity never shrinks
back down, so growth happens repeatedly across 5 runs. A and B, by
contrast, allocate many small nodes individually through glibc's allocator,
which recycles already-faulted-in pages from its free list rather than
requesting large fresh spans from the OS on every growth step. So C trades
"no allocation in the hot path" for "occasional large, page-fault-heavy
reallocations of its order table" — a direct illustration of the general
rule that a single large contiguous allocation isn't free just because it
avoids per-operation `malloc` calls.

### 9.5 Production Choice

**I would deploy Implementation C**, but the decision isn't just "it has the
lowest average latency" — a few other properties matter more for a real
trading system:

- **Latency and predictability:** C's `add_order`/`cancel_order` are O(1)
  with no data-dependent branching and no allocation in the common path, so
  its latency distribution should be tight — exactly what a system reading
  BBO millions of times a second needs. A's tree-based approach has
  logarithmic *and* allocation-dependent latency, meaning its tail latency
  (not just its average) is worse and less predictable than its mean
  suggests, since rebalancing and allocator behavior vary per call.
- **Memory usage:** this is C's real weakness. The flat array is fixed-size
  and cheap (~80 KB for 10,001 ticks), but the order table is sized to the
  *largest order id ever seen*, not to the number of live orders — for an id
  space with large gaps or very large ids, this wastes memory that A/B's
  hash-based order storage would not. In production, this assumption (dense,
  bounded ids) would need to hold, or the design would need a hybrid id
  allocator/remapping layer.
- **Cache behavior:** clearly the best of the three, as shown above, and this
  compounds under real multi-core contention (less cache pollution means
  fewer evictions of other cores' working sets sharing LLC).
- **Complexity and maintainability:** C is actually simpler to read than A —
  no tree/list bookkeeping, no iterator invalidation concerns, just index
  arithmetic — but it is more special-cased (fixed price bands, assumes
  dense ids) and the best-price rescan is a subtle piece of logic that needs
  good test coverage (exactly what `--verify` exists for). A is the most
  "obviously correct" design at a glance because it leans entirely on
  standard library containers.

Given the instrument here has a genuinely fixed, small price-level universe
(the whole point of this assignment), C's assumptions hold cleanly, and I'd
take the latency/cache win. If the id space were unbounded/sparse in a real
deployment, I'd pair C's array-based price-level storage with B's hashed
order lookup instead of C's dense vector — getting the best-price win
without the page-fault-heavy growth.

### 9.6 What Would You Try Next?

- **Reduce timing-instrumentation overhead.** The `perf report` for C shows
  ~20% of its sampled cycles landing inside `std::chrono::steady_clock::now()`
  itself — once the real work is this cheap, the per-operation timer calls
  in the benchmark harness become a significant fraction of what's being
  measured. I'd batch timing (time blocks of N operations instead of every
  single one) to get a cleaner signal on C's true per-op cost.
- **Fix C's page-fault/memory-footprint issue** by reserving the order
  vector's capacity up front (if a reasonable max order id is known) instead
  of doubling reactively, or switching to a hybrid id→slot map for sparse id
  spaces.
- **Object pooling for A.** Since the ~10% malloc/free overhead in A is the
  single biggest identified cost, a custom allocator or free-list/object pool
  for `std::map` nodes and `std::list` nodes would likely close much of the
  gap to B without changing A's overall tree-based design.
- **`perf record -g --call-graph=dwarf`** for a trustworthy deep call graph
  on B and C — the default frame-pointer-based stack walker breaks down
  under `-O2` (since frame pointers are omitted), which is why this
  submission's deeper call trees for B and C show unresolved addresses a few
  levels in, even though the top-level self-time percentages are accurate.
- **Branch reduction in A**, e.g. replacing `std::map`'s generic rebalancing
  comparisons with a flatter structure (a sorted `std::vector` of price
  levels, since the number of distinct levels is bounded) to cut down the
  data-dependent branches identified in 9.2.

## 10. Files

- `Common.h` — shared `Side`/`Level`/`BBO` types, tick conversion helpers
- `OrderBookA.h` / `.cpp` — Implementation A (`std::map` + `std::list`)
- `OrderBookB.h` / `.cpp` — Implementation B (`unordered_map` + `std::set`)
- `OrderBookC.h` / `.cpp` — Implementation C (flat array, own design)
- `main.cpp` — sanity-check demo, runs the same scenario through A/B/C
- `bench.cpp` — benchmark (`--impl A|B|C`) and correctness harness (`--verify`)
- `run_perf.sh` — runs `perf stat`/`perf record` for all three implementations on Linux
- `perf_results/` — saved perf output from this submission's benchmark run
- `Makefile` — builds `demo` and `bench`
