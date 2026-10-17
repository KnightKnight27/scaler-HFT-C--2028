# Capstone: Low-Latency Order Book

- **email**: sarthak.24bcs10149@sst.scaler.com
- **roll_no**: 24BCS10149

A single-instrument order book (orders just rest, no matching), built **three different ways**
and benchmarked + profiled with `perf`. One instrument, tick size `0.01`, bids in `[50.00, 99.99]`,
asks in `[100.01, 150.00]`, mid `100.00` → 10,000 possible price levels. Single-threaded, standard
library only, compiled with `-O2`.

- **A** → `std::map` (balanced tree)
- **B** → `std::priority_queue` + lazy deletion
- **C** → my own design: price-indexed flat arrays

---

## Build

One g++ command:

```bash
g++ -O2 -std=c++17 bench.cpp impl_a_map.cpp impl_b_pqueue.cpp impl_c_array.cpp -o bench
```

Or with CMake:

```bash
mkdir build && cd build
cmake ..
make
# the bench binary is then in build/
```

## Run

```bash
./bench --verify      # check all 3 give identical results
./bench --impl A      # benchmark A  (also B, C)
```

Anything else prints a short usage message. There is exactly **one** benchmark binary — the
implementation is picked by the `--impl` flag, so `perf` just wraps the same program
(`perf stat -r 5 ./bench --impl A`).

## Files

| file | what it is |
|------|-----------|
| `orderbook.h`                      | API structs (`Side`, `Level`, `BBO`) + shared `IOrderBook` interface + tick helpers |
| `impl_a_map.h` / `.cpp`            | Implementation A (`std::map`) |
| `impl_b_pqueue.h` / `.cpp`         | Implementation B (`priority_queue` + lazy deletion) |
| `impl_c_array.h` / `.cpp`          | Implementation C (flat arrays, my design) |
| `bench.cpp`                        | the one benchmark + `--verify` |
| `perf_results/`                    | saved `perf stat` / `perf report` output |

Each implementation is split into a header (class declaration) and a `.cpp` (method definitions);
`bench.cpp` includes the three headers and all four `.cpp` files are compiled together. All three
classes derive from the same abstract `IOrderBook`, so the benchmark drives them through a single
`IOrderBook*` and nothing in `bench.cpp` is implementation-specific except the one line that picks
which object to `new`.

## Interface changes

I kept the function signatures exactly as given in the spec (still `void add_order(uint64_t, Side,
double, long)`, still `double price`). The only change is **internal**: prices are stored as
integer **ticks** (`tick = llround(price * 100)`, so `100.01 → 10001`). I use `llround` and not a
plain cast because `100.01 * 100` is actually `10000.999…` in floating point, and a `(int)` cast
would truncate it to `10000` — the wrong level. Storing ticks avoids float-compare issues entirely
and lets C use the tick directly as an array index. Bands as ticks: bids `5000..9999`, asks
`10001..15000`, which is the 10,000 levels the spec describes. These bounds live as the
`BID_MIN_TICK … ASK_MAX_TICK` macros in `orderbook.h`.

## Required behaviour (edge cases)

All three implementations handle the spec's required edge cases, and `--verify` checks them as part
of the random workload:

- **Cancel unknown / already-cancelled order** → returns `false`, never crashes. A and B look the id
  up in their hash map and return `false` on a miss; C checks the per-id `live` flag (an unknown or
  already-cancelled id reads back `live == false`).
- **Empty book** → `get_bbo()` returns `has_bid == false` and `has_ask == false` (the `BBO` is
  value-initialised, so both flags start `false` and are only set when a side actually has volume).
- **Insufficient depth** → `get_bids(10)` on a book with only 3 levels returns those 3 levels; the
  depth argument is an upper bound, not a requirement.

---

## The three designs

The spec asks for three *genuinely* different designs. These differ in how they store orders, how
they store price levels, how they find the best price, and how cancel locates an order — summarised
here and detailed per design below.

| | A — map | B — pqueue | C — arrays |
|---|---|---|---|
| orders (id → info) | `unordered_map` | `unordered_map` | `vector` indexed by id |
| price levels | balanced tree `map<tick,qty>` | `unordered_map<tick,qty>` + heap of ticks | flat `vector<long>` indexed by tick |
| best price | tree `begin()` | heap `top()` (after popping dead ticks) | a cached cursor index |
| cancel finds order by | hash lookup on id | hash lookup on id | direct `orders[id]` |

### A — `std::map`
- **Orders:** `unordered_map<uint64_t, OrderRec>` mapping id → `{side, tick, qty}`.
- **Levels:** two trees, `map<int,long,greater<int>>` for bids and `map<int,long>` for asks. The
  `greater<int>` comparator reverses the bid tree so the **highest** bid is at `begin()`; asks use
  the default ascending order so the **lowest** ask is at `begin()`. The value is the summed qty at
  that level.
- **Best price:** just `bids.begin()` / `asks.begin()` → O(1) (the tree keeps itself sorted).
- **Cancel:** hash-lookup the id to get its side+tick+qty, subtract that qty from the level in the
  tree, and if the level hits zero `erase` the whole node. Then erase the id from the hash. So one
  hash lookup **and** a tree erase — that's why A's cancel is the slowest of the three.
- **Depth:** walk the tree from `begin()` for `depth` steps — already in price order.

| op | add | cancel | bbo | bids | asks |
|----|-----|--------|-----|------|------|
| complexity | O(log n) | O(log n) | O(1) | O(depth) | O(depth) |

### B — `priority_queue` + lazy deletion
- **Orders:** `unordered_map<uint64_t, OrderRec>`, same as A.
- **Levels:** the quantity per level lives in `unordered_map<tick,long>` (one per side). Separately,
  a max-heap of bid ticks and a min-heap of ask ticks remember *which* prices exist, so the best
  price is the heap `top()`. The two structures are kept in sync on add (push the tick the first
  time a level goes from 0 → non-zero).
- **Best price:** `top()` of the heap — but a heap can't delete from the middle, so before reading it
  I pop any "dead" ticks off the top whose qty is now 0 (`clean_bids`/`clean_asks`). Amortised
  O(log n).
- **Cancel:** hash-lookup the id, then just `bidQty[tick] -= qty` and erase the id. I deliberately do
  **not** touch the heap — that's the "lazy deletion". The stale tick stays in the heap until it
  bubbles to the top and gets popped during a later `get_bbo`. So cancel is O(1), but the heap grows
  with stale entries.
- **Weak spot:** `get_bids/get_asks` has to **copy** the whole heap and pop it, because a heap has no
  sorted iteration. It also skips duplicate ticks (a level that emptied and refilled gets pushed
  twice). This copy+pop is the slow part of B.

| op | add | cancel | bbo | bids | asks |
|----|-----|--------|-----|------|------|
| complexity | O(log n) | O(1) | O(log n)\* | O(n log n) | O(n log n) |

\* amortized (popping dead tops)

### C — flat arrays (my design)
Only 10,000 prices can ever exist, so I don't search for a price — I use the tick *as* the index.
- **Orders:** a flat `vector<OrderRec2>` indexed **directly by id**. Ids are handed out densely
  (0, 1, 2, …), so there's no hashing at all — just `orders[id]`. Each record carries a `live` flag
  so I can tell "unknown / already-cancelled" (`live == false`) from a real order. The id vector is
  grown by **doubling** (`while (ns <= id) ns *= 2`), which keeps appends amortised O(1); resizing to
  `id+1` every time would re-copy the whole vector on each add → O(n²).
- **Levels:** two flat `vector<long>` of 5000 slots each (bids/asks), index = `tick − band_min`, value
  = summed qty. Contiguous memory, no nodes, no pointers.
- **Best price:** I cache `bestBidIdx` / `bestAskIdx` cursors. On add I nudge the cursor outward if the
  new price is better, so `get_bbo` is a plain O(1) array read — no scanning 5000 slots.
- **Cancel:** O(1) in the common case — `orders[id]` gives side+tick+qty, subtract from the level. The
  only extra work is when I just emptied the *current best* level: then I walk the cursor inward to
  the next non-empty slot. That walk is O(L) worst case, but it's a linear scan over contiguous
  memory (prefetcher-friendly) and in practice only a handful of slots.
- **Depth:** walk outward from the cursor collecting non-empty slots until `depth` is reached.

| op | add | cancel | bbo | bids | asks |
|----|-----|--------|-----|------|------|
| complexity | O(1) | O(1)\* | O(1) | O(L)† | O(L)† |

\* O(L) worst if it has to rescan for the next best level  †usually tiny

---

## Correctness

`./bench --verify` builds the full 1,000,000-op random workload (same fixed seed as the benchmark),
replays it through all three books in lockstep, and after **every** operation compares C and B
against A (the reference) on: the `cancel` return value, the full BBO (prices + quantities + the
has_bid/has_ask flags), and — every 100 ops — the top-10 bid and ask levels (prices + quantities +
count). If anything ever disagrees it reports `FAIL`.

```
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

All three also finish with the same final BBO (`Bid 99.99 x 2722`, `Ask 100.01 x 2525`) and the same
checksum `843022881`, which is an independent cross-check on top of the per-op comparison.

---

## Benchmark

One program, implementation selected by `--impl` (never three separate programs). The **same**
workload drives all three, because `build_workload` records every operation into a `vector<Op>`
*once* using a fixed seed, and each book just replays that identical list.

**Workload** (per the spec):
- 1,000,000 operations total, `std::mt19937 rng(42)` so it's repeatable.
- Mix: ~70% add, ~20% cancel, ~10% BBO query. (A cancel only happens if at least one order is live;
  otherwise that draw becomes a BBO query.)
- Every 100 operations, also call `get_bids(10)` and `get_asks(10)`.
- Side 50/50 buy/sell, quantity 1–100, price a random valid tick in the correct band for the side.
- Cancels always target a **currently-live** order: I keep a `live` vector of ids and swap-remove the
  chosen one (O(1)), so a cancel can never miss during the main workload.

**Measurements:** `std::chrono::steady_clock`, timing each `add` / `cancel` / `get_bbo` separately
(two `now()` calls around each op, accumulated per type). I report total time and average ns per op
type. Each implementation runs the workload **5 times** and I report the **median** of the 5 (sort,
take the middle) so one noisy run doesn't skew the result. The final BBO and a checksum are printed
at the end so `-O2` can't optimise the book work away.

---

## Results

Machine: Intel **i5-13500H**, Pop!_OS, g++ 13.3, `-O2`. On this hybrid CPU the work runs on the
P-cores, so the `cpu_core` counters below are the real ones (`cpu_atom`/E-core rows are ~0 and
noisy). `./bench --impl X` runs the workload 5× internally, so the perf counts are ~5× one run —
same for all three, so the comparison is fair. Raw output in `perf_results/`.

| Metric | A (map) | B (pq) | C (array) |
|--------|--------:|-------:|----------:|
| Total time (ms) | 193.3 | 141.0 | **52.8** |
| ns / add     | 165 | 87 | **26** |
| ns / cancel  | 271 | 177 | **58** |
| ns / BBO     | 21 | 34 | **19** |
| IPC          | 0.96 | 1.38 | **1.88** |
| Branch miss % | **5.03** | 1.06 | 1.57 |
| L1-dcache misses | 65 M | **85 M** | 5.3 M |
| Cache misses | 20 M | 17 M | **3.0 M** |
| Page faults  | 18,297 | **28,776** | 23,183 |

(Bold = the extreme value for that row.)

### Raw `perf stat` output

Collected with `perf stat -r 5 ./bench --impl X` (full logs, including `perf record`
reports, are in `perf_results/`). Only the P-core (`cpu_core`) counters are shown — on this
hybrid CPU the E-core (`cpu_atom`) rows are `<not counted>`/noise, and
`L1-dcache-load-misses` is `<not supported>` on the atom side.

**Implementation A — `std::map`**
```
          1,160.35 msec task-clock:u                       ( +-  1.84% )
     4,219,316,696      cpu_core/cycles/u                   ( +-  1.47% )
     4,054,225,264      cpu_core/instructions/u             ( +-  0.01% )   # IPC 0.96
       791,112,211      cpu_core/branches/u                 ( +-  0.01% )
        39,812,980      cpu_core/branch-misses/u            ( +-  0.02% )   # 5.03%
        19,549,571      cpu_core/cache-misses/u             ( +-  4.09% )
        64,550,678      cpu_core/L1-dcache-load-misses/u    ( +-  0.10% )
         3,420,292      cpu_core/dTLB-load-misses/u         ( +-  1.10% )
            18,297      page-faults:u                       ( +-  0.00% )

       1.163280224 +- 0.021494415 seconds time elapsed     ( +-  1.85% )
```

**Implementation B — `priority_queue` + lazy deletion**
```
            874.98 msec task-clock:u                        ( +-  0.67% )
     3,095,207,377      cpu_core/cycles/u                    ( +-  0.35% )
     4,264,595,795      cpu_core/instructions/u              ( +-  0.00% )   # IPC 1.38
       773,206,333      cpu_core/branches/u                  ( +-  0.01% )
         8,192,739      cpu_core/branch-misses/u             ( +-  0.05% )   # 1.06%
        16,582,404      cpu_core/cache-misses/u              ( +-  0.57% )
        84,776,985      cpu_core/L1-dcache-load-misses/u     ( +-  0.37% )
         3,438,985      cpu_core/dTLB-load-misses/u          ( +-  0.58% )
            28,776      page-faults:u                        ( +-  0.00% )

       0.877620765 +- 0.006005230 seconds time elapsed      ( +-  0.68% )
```

**Implementation C — flat price-indexed arrays**
```
            306.59 msec task-clock:u                         ( +-  0.46% )
     1,057,125,625      cpu_core/cycles/u                     ( +-  0.21% )
     1,992,511,462      cpu_core/instructions/u               ( +-  0.00% )   # IPC 1.88
       332,789,582      cpu_core/branches/u                   ( +-  0.00% )
         5,223,110      cpu_core/branch-misses/u              ( +-  0.07% )   # 1.57%
         3,039,278      cpu_core/cache-misses/u               ( +-  1.33% )
         5,313,053      cpu_core/L1-dcache-load-misses/u      ( +-  0.06% )
           251,964      cpu_core/dTLB-load-misses/u           ( +-  1.38% )
            23,183      page-faults:u                         ( +-  0.00% )

       0.308501319 +- 0.001448194 seconds time elapsed       ( +-  0.47% )
```

---

## Analysis

**Which is fastest?**
C wins every operation (add, cancel, BBO) and is ~4× faster overall than A. It just does array
indexing — no tree balancing, no hashing, no heap. A is slowest at cancel because it does a hash
lookup *and* a tree erase (and the erase can trigger rebalancing + a `free`). B's cancel is O(1) but
still pays two hash lookups (id map, then qty map). C's add/cancel are plain array writes, which is
why they're 3–6× quicker than B and ~5–6× quicker than A.

> Note: `perf record` shows ~47% of C's time is the `clock_gettime` calls I wrap around each op.
> C's ops are so fast the timer dominates, so its real per-op cost is even lower than shown. That's
> also why A's and C's BBO look almost equal (~19–21 ns) — both are near the timer floor, and BBO is
> O(1) for both (tree `begin()` vs a cached cursor), so there's barely any real work left to separate
> them above the measurement noise.

**Branch prediction — who misses most?**
A (map), 5% misses, lowest IPC. Walking a red-black tree is a chain of compare-then-go-left-or-right
branches, and which way you go depends on the data, so the CPU can't predict them → pipeline stalls.
B is low (1%) because its hot path is hashing + a shallow heap fix-up. C's code is straight-line
array math with predictable branches → nearly 2× A's IPC, even though its one data-dependent branch
(the cancel cursor-walk) nudges it slightly above B's miss rate.

**Cache behaviour — who misses most?**
C has the fewest by far. It reads **contiguous** arrays, so neighbours share cache lines and the
hardware prefetcher helps. A scatters tree/hash nodes all over the heap → every lookup is a pointer
chase that misses cache. B has the most **L1** misses because it holds two hash maps *and* a heap
that keeps growing with dead entries → the biggest, most scattered working set (lots of small
heap-allocated nodes, pointer chasing, and rehashing). In short: C accesses contiguous memory, A
follows pointers, B follows pointers **and** allocates the most small objects.

**Page faults — who causes most?**
B. It allocates the most growing, scattered memory (hash maps rehashing as they grow + the heap
growing from lazy dead entries that never get removed until popped). C is next, but for a good
reason — a one-time up-front `reserve`/`assign` of the arrays and the id vector, so the faults happen
once at startup and then never again (ideal for steady-state latency). A has the fewest because the
*live* book stays small (cancelled levels are erased immediately, so its footprint doesn't balloon).

**Which one to deploy?**
**C.** Not just because it's fastest on average, but because it's **predictable**: straight-line code
+ contiguous memory = tight, repeatable latency with no surprise spikes, and HFT cares about the
tail (p99/p99.9) far more than the mean. Memory is fixed and known up front (no rehash/rebalance
mid-trade), cache behaviour is the best of the three, and the code is simple enough to read and
reason about. The trade-offs: it *assumes* a fixed price band and dense ids, and it uses a fixed
~10,000-slot footprint even when the book is nearly empty. For this spec (one instrument, fixed tick,
sequential ids) those assumptions always hold, so the cost is paid once and bounded. If the price
range were unbounded or ids were sparse I'd fall back to the map (A) for safety, trading latency for
generality — a slower-but-predictable tree beats a fast structure that occasionally rehashes or
rebalances right in the hot path.

**What I'd try next**
- Integer ticks in the public API too (drop the `double`) — removes the `llround` on every add.
- Stop timing each op with `clock_gettime` (it's ~half of C's measured time) — time in batches or use
  `rdtsc` so the measurement stops dominating the fast path.
- In C, replace the "walk to next best level" scan with a **bitset + `ctz`** (count-trailing-zeros)
  for a true O(1) next-best, killing the one data-dependent branch.
- Object pool / pre-allocation for A and B (A spent a lot of time in `free` on erase).
- Custom / faster hash for A and B's id lookup — the std hashtable `find` was their hottest function
  in `perf report`.
- Avoid the heap copy in B's `get_bids/get_asks` (keep a sorted side-structure, or cap how deep the
  stale entries can pile up).
