# Capstone: Order Book, Three Ways

**Name:** Vansh Malhotra
**Roll No:** 24BCS10268
**SST Email:** vansh.24bcs10268@sst.scaler.com
**Batch:** B

A resting limit order book for one instrument, built with three different data structures, checked against each other, benchmarked on the same 1,000,000-op workload and profiled with `perf`.

```
Capstone/
├── OrderBook/
│   ├── common.hpp       Side, Level, BBO, Order, tick helpers
│   ├── book_map.hpp     Implementation A  (std::map)
│   ├── book_heap.hpp    Implementation B  (unordered_map + priority_queue)
│   ├── book_array.hpp   Implementation C  (flat array + bitmap, my design)
│   ├── bench.cpp        benchmark + --verify
│   └── Makefile
├── run_perf.sh          runs verify, benchmarks, perf stat, perf record and saves everything to results/
└── results/             raw output used in this report
```

---

## 1. Build

Needs a C++17 compiler. Standard library only.

```bash
cd OrderBook
make                    # g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -o bench bench.cpp
```

or without make:

```bash
g++ -std=c++17 -O2 -o bench bench.cpp
```

(`-g -fno-omit-frame-pointer` only add symbols for `perf record`. The generated code is still `-O2`.)

## 2. How to Test

**Step 1: Correctness test**

```bash
./bench --verify        # or: make verify
```

It does two things:

1. **Edge case tests.** Hand-written cases with answers I worked out myself, run separately on A, B and C:
   - empty book: `has_bid == false`, `has_ask == false`, no levels
   - cancelling an unknown id returns `false`, and cancelling the same id twice returns `false` the second time
   - several orders at one price add up into one level
   - insufficient depth: 3 bid levels exist, `get_bids(10)` returns those 3, best price first
   - `get_bids(0)` returns nothing
   - out-of-band prices (a buy at 120.00, a sell at 99.00) are rejected
   - best bid/ask moves to the next level when a level empties, and a level can be reused after it was emptied
   - huge order ids (`1 << 40`) work, which tests C's fallback path
   - a duplicate order id is ignored
2. **Lockstep comparison.** The same random workload goes through all three books at the same time. After **every** operation it compares the cancel return value, the BBO, `get_bids(10)` and `get_asks(10)` (price and qty of every level). There are three workloads:
   - the real benchmark workload (1M ops, seed 42, whole price range)
   - a narrow book (only 20 ticks per side), so levels empty and the best price moves all the time
   - a tiny book (2 ticks per side), mostly to hit the "side becomes empty" paths

Expected output (saved in [`results/verify.txt`](results/verify.txt)):

```
Running correctness test...

Edge case tests:
  Implementation A: PASS
  Implementation B: PASS
  Implementation C: PASS

Lockstep comparison (BBO + 10 bid levels + 10 ask levels after every op):
  benchmark workload (1M ops, seed 42, full price range)   1000000 checks, 0 mismatches
  narrow book (200k ops, seed 7, 20 ticks per side)        200000 checks, 0 mismatches
  tiny book (200k ops, seed 99, 2 ticks per side)          200000 checks, 0 mismatches

All implementations produce identical results.
```

The program exits with code 1 if anything fails. To make sure the test can actually fail, I broke a copy of the code on purpose: C picked the wrong next best bid, and B skipped duplicate heap entries wrongly. `--verify` reported about 2 million mismatches and `Implementations DISAGREE.`

**Step 2: Benchmark each implementation**

```bash
./bench --impl A
./bench --impl B
./bench --impl C
./bench                  # all three one after another
./bench --impl C --runs 10
./bench --impl C --untimed   # no per-op clock calls, total time only (used for perf record)
```

Each one prints every run, then the median of 5 runs, the final BBO and a checksum. **The checksum must be the same for A, B and C.** It hashes every BBO, every depth snapshot and every cancel result, so if the three agree the books did the same work. (The checksum differs between macOS and Linux because libc++ and libstdc++ implement `uniform_int_distribution` differently. On the same machine it is identical for all three.)

**Step 3: perf (Linux only)**

On a Linux machine:

```bash
./run_perf.sh
```

On macOS (what I used, since there is no `perf` on Mac) it runs inside a privileged Debian container:

```bash
docker run --rm --privileged -v "$PWD":/work -w /work debian:bookworm bash -c \
  "apt-get update -qq && apt-get install -y -qq g++ make linux-perf >/dev/null && ./run_perf.sh"
```

For each implementation the script runs:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X
perf record -F 20000 -g ./bench --impl X --untimed --runs 20
perf report --stdio --no-children --sort dso,symbol
```

and writes everything to `results/`.

---

## 3. Interface changes

The API is the one from the brief: same names, same `Side` / `Level` / `BBO` structs, prices as `double`. The only changes are inside the books:

- Every book converts the price to an **integer tick** on entry (`lround(price * 100)`, so 99.99 → 9999). Tick is what gets stored and compared. Prices go back to `double` only when building a `Level` / `BBO`. No book uses a `double` as a key.
- Invalid input is ignored and changes nothing: a price outside the side's band, `qty <= 0`, or an id that is already live.
- The benchmark uses the three classes as **template parameters** (`run_once<Book>`), not through a virtual base class. All three have the same five methods, so they share the same logical interface, and there is no virtual call cost in the timings.

---

## 4. The three implementations

| | A: `std::map` | B: hash map + heap | C: array + bitmap (mine) |
|---|---|---|---|
| Orders stored in | `std::map<uint64_t, Order>` (red-black tree) | `std::unordered_map<uint64_t, Order>` | `std::vector<Order>` indexed directly by id (`unordered_map` only for ids ≥ 2²⁴) |
| Price levels stored in | `std::map<int,long,greater>` bids, `std::map<int,long>` asks | `std::unordered_map<int,long>` per side | one flat `long qty[10001]`, one slot per tick |
| Best price found by | `begin()` of the map | top of a max-heap (bids) / min-heap (asks), stale tops popped lazily | cached `best_bid` / `best_ask`, recomputed from a 2-level bitmap with `clz`/`ctz` when the best level empties |
| Order found on cancel by | tree search on id | hash lookup on id | `orders[id]`, a plain array index |
| Depth (`get_bids`) | iterate the map from `begin()` | copy the heap, pop until `d` live distinct ticks | jump from set bit to set bit in the bitmap |

### A: `std::map` everywhere

The most obvious design. Levels are kept sorted by the tree, so the best bid is `bids.begin()` (map sorted descending) and the best ask is `asks.begin()`. Cancel finds the order in the order map, subtracts its qty from its level and erases the level if it reaches 0. Every order and every new level is a separate heap-allocated tree node (~48–64 bytes) placed somewhere in memory.

### B: hash maps + binary heaps with lazy deletion

Orders and level quantities are in `unordered_map`s, so add and cancel are O(1) on average. For the best price each side has a `std::priority_queue<int>` of ticks. A tick is pushed when its level goes from empty to non-empty. On cancel, if the level becomes empty, it is erased from the hash map, **but the tick stays in the heap**, because a heap cannot remove from the middle. `get_bbo()` first pops stale ticks off the top (ticks with no level in the map). The heaps are `mutable` for this reason. A level that empties and refills gets pushed again, so duplicates can exist; `get_bids` skips them because duplicates come out next to each other.

### C: price-indexed array + two-level bitmap + id-indexed orders (my design)

There are only 10,001 possible ticks, so a level is just `qty[tick - 5000]`, 80 KB in total, which fits in L2. No searching and no allocation.

The hard part is finding the next best level when the best one empties. Walking the array tick by tick can take up to 5000 steps. Instead I keep a bitmap per side: `w[157]` (one bit per tick: is this level non-empty) and `top[3]` (one bit per word of `w`: is this word non-zero). To find the next lower bid I mask the current word and take `63 - __builtin_clzll(word)`. If that word is empty I look in `top` for the next non-zero word, and take its highest bit the same way. That is at most a few instructions and 3 summary words to check, so it is effectively O(1) no matter how sparse the book is. `get_bids` / `get_asks` use the same `prev` / `next` calls to jump between non-empty levels.

Orders: IDs are unique, never reused and (in the benchmark) increasing, so `orders[id]` is a direct index into a vector. `qty == 0` means "not live". Ids ≥ 2²⁴ go to an `unordered_map` so that one huge id can't make the vector allocate gigabytes (tested in `--verify`).

### Complexity

N = live orders, L = non-empty levels on a side, H = heap size (live + stale ticks), d = depth.

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | O(log N + log L) | O(1) avg, + O(log H) if level is new | O(1) amortized |
| `cancel_order` | O(log N + log L) | O(1) avg | O(1) (bitmap jump is at most 3 words) |
| `get_bbo` | O(1) | O(1) amortized (each stale pop is O(log H), paid for by an earlier cancel) | O(1) |
| `get_bids(d)` | O(d) | O(H + d log H) (heap copy) | O(d) |
| `get_asks(d)` | O(d) | O(H + d log H) | O(d) |

---

## 5. Workload

`make_workload(1'000'000, 42)` builds the whole op list **before** timing starts, so generating random numbers is not part of the measurement. Everything uses `std::mt19937 rng(42)`.

- 70% add, 20% cancel, 10% `get_bbo` (if no order is live, a cancel becomes an add)
- side 50/50, qty uniform 1–100, price a uniform random tick in [50.00, 99.99] for bids and [100.01, 150.00] for asks
- a cancel always picks a random **currently live** id (vector of live ids with swap-and-pop)
- `get_bids(10)` and `get_asks(10)` every 100 ops
- each add / cancel / bbo call is timed on its own with `std::chrono::steady_clock`. Median of 5 runs.

---

## 6. Results

**Machine:** Apple M4 MacBook (10 cores). The perf numbers come from a Debian 12 container under Docker Desktop (Linux 6.10.14-linuxkit, g++ 12.2, `-O2`, perf 6.1).

### Results table (Linux container, median of 5 runs)

| Metric | A (`std::map`) | B (hash + heap) | C (array + bitmap) |
|---|---|---|---|
| Total time (ms) | 280.5 | 97.1 | **40.9** |
| Total time, no per-op timers (ms) | 259.1 | 54.2 | **16.1** |
| ns / add | 178.3 | 50.4 | **20.7** |
| ns / cancel | 719.2 | 134.3 | **36.4** |
| ns / BBO | 25.5 | 28.1 | **22.9** |
| task-clock, whole process incl. 5 runs + workload gen (ms) | 1414 | 587 | **296** |
| IPC | not supported | not supported | not supported |
| Branch misses (%) | not supported | not supported | not supported |
| L1-dcache-load-misses | not supported | not supported | not supported |
| Cache misses | not supported | not supported | not supported |
| dTLB-load-misses | not supported | not supported | not supported |
| Page faults | 10,594 | **22,670** | 1,223 |

**Unavailable counters:** `cycles`, `instructions`, `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and `dTLB-load-misses` all show `<not supported>`. Docker Desktop on Apple Silicon runs Linux in a VM that does not pass the hardware PMU through to the guest, so only software events (`task-clock`, `page-faults`, `cpu-clock` sampling) work. macOS itself has no `perf`. So IPC, branch and cache counts are not in the table. The cache and branch discussion below is based on the code plus the `perf record` profiles. Running `./run_perf.sh` on a lab Linux machine would fill in the hardware rows with no code changes.

Native macOS (Apple clang 21, same `-O2`) for comparison. Same ranking, everything ~1.5–2× faster than in the VM:

| | A | B | C |
|---|---|---|---|
| Total (ms) | 145.3 | 64.6 | 32.4 |
| ns / add | 92.2 | 22.8 | 17.0 |
| ns / cancel | 311.6 | 80.1 | 18.6 |
| ns / BBO | 13.8 | 16.5 | 13.6 |

### Benchmark output (`results/bench_*.txt`)

All three end with the same BBO and checksum:

```
Final BBO:
  Bid: 99.99 x 2573
  Ask: 100.01 x 2525
Checksum: 7537248685437943589
```

### perf stat: Implementation A

```
 Performance counter stats for './bench --impl A' (5 runs):

           1414.23 msec task-clock                       #    0.868 CPUs utilized            ( +-  9.53% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             10594      page-faults                      #    6.507 K/sec

             1.628 +- 0.135 seconds time elapsed  ( +-  8.28% )
```

### perf stat: Implementation B

```
 Performance counter stats for './bench --impl B' (5 runs):

            587.20 msec task-clock                       #    1.024 CPUs utilized            ( +-  1.75% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             22670      page-faults                      #   39.534 K/sec                    ( +-  0.00% )

            0.5736 +- 0.0103 seconds time elapsed  ( +-  1.80% )
```

### perf stat: Implementation C

```
 Performance counter stats for './bench --impl C' (5 runs):

            296.40 msec task-clock                       #    0.872 CPUs utilized            ( +-  6.97% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              1223      page-faults                      #    3.603 K/sec

            0.3398 +- 0.0207 seconds time elapsed  ( +-  6.10% )
```

### perf record (`results/perf_report_*.txt`, top symbols, `--untimed --runs 20`)

```
A:  42.55%  MapBook::cancel_order
    31.37%  MapBook::add_order
     6.83%  std::_Rb_tree<unsigned long, pair<..., Order>>::...   (order-map lookup)
     4.67%  std::_Rb_tree_rebalance_for_erase
     4.26%  std::_Rb_tree_insert_and_rebalance
     2.02%  libc cfree

B:  16.99%  libc 0x923d8   } unresolved libc addresses (container libc has no symbols),
    10.75%  libc 0x9d210   } most likely malloc/free and memcpy of the heap copies
     8.35%  HeapBook::add_order
     8.27%  run_once<HeapBook>     (get_bids/get_asks/get_bbo inlined here)
     2.93%  std::__adjust_heap     (heap pops)
     2.63%  unordered_map<int,long>::operator[]

C:  41.02%  run_once<ArrayBook>    (benchmark loop + inlined get_bbo/get_bids/get_asks + checksum)
    39.82%  ArrayBook::add_order
     4.79%  vector<Order>::_M_fill_insert
     3.29%  ArrayBook::cancel_order
     1.80%  lround@plt
```

perf also found a real problem in C. In my first version, `add_order` grew the order vector with `orders.resize(id + 1)` on every new id. perf showed `_M_fill_insert` at **14%** of C's time. I changed it to double the size instead (`resize(max(id + 1, 2 * size))`), and `_M_fill_insert` dropped to ~5%. The untimed total went from 12.9 ms to 10.5 ms in back-to-back container runs. (The final run in the table measured 16.1 ms. The VM is noisy at this scale, so I trust the drop in the profile more than the ms.)

---

## 7. Performance analysis

### 7.1 Which implementation is fastest?

**C is fastest for all three operations:** add 20.7 ns, cancel 36.4 ns, BBO 22.9 ns. Without the per-op timers the whole 1M-op run takes 16 ms for C, 54 ms for B and 259 ms for A. C is about 16× faster than A and 3.4× faster than B.

- **add:** C does one array write for the order (`orders[id] = ...`), one `+=` on `qty[i]`, maybe one bit set, and two compares for the cached best. It never allocates or hashes. B has to hash the id, allocate a node for the order and maybe for a new level, and sometimes push onto the heap. A walks two red-black trees (~20 levels deep for 500k orders), allocates a node and rebalances. That is why `add_order` + `_Rb_tree_insert_and_rebalance` take ~35% of A's profile.
- **cancel:** cancels are the slowest op everywhere, because a cancel picks a *random old* order, and its data has usually already left the cache, while adds touch memory that is still warm. A is the worst at 719 ns. It has to search a tree of ~500k nodes, where every step is a pointer to another node somewhere in memory, likely a cache miss each time, and then rebalance on erase. `MapBook::cancel_order` is the #1 symbol at 42.5%. B makes one hash lookup (one or two misses). C makes one direct index, at most one miss.
- **BBO:** all three are close (23–28 ns), and most of that is the cost of the two `steady_clock::now()` calls themselves. A reads `begin()` (cached by the map), and C reads two ints. B is a bit slower because `get_bbo()` first has to check the heap top against the hash map (`levels.count(top)`), which is a hash lookup on every call.

Why C wins: it does less work per operation, and the work it does is on small, contiguous memory. The level array (80 KB) and bitmaps (~2.5 KB) stay in L1/L2, the order vector is written sequentially, and nothing is allocated in the hot path.

Side note: C is so fast that in its profile the benchmark loop itself (`run_once`, which includes the depth calls and checksum hashing) costs as much as the book. The timer calls are also a big part: with timers C takes 41 ms, without them 16 ms. So C's per-op numbers are close to the resolution limit of timing every single call.

### 7.2 Branch prediction

The branch-miss counter was not available in my environment (see section 6), so this is from reading the code, not from measurement. I would expect **A to have the most branch misses**. A tree search makes a `key < node.key` decision at every level, and with random prices and random ids those decisions are basically coin flips, so the predictor can't learn them. That is ~20 hard-to-predict branches per lookup, two lookups per cancel. Red-black rebalancing adds more data-dependent branches (colour checks, rotate left or right).

B has fewer: hashing has no data-dependent branches, but the bucket chain walk and `if (q == 0) push` are unpredictable, and the heap sift-down during pops compares children (left or right?), which is random. C's branches are mostly predictable. `if (qty[i] == 0)` is almost always false in a book with ~50 orders per level, and `i > best_bid` is rarely true. The bitmap search replaces a loop ("scan until non-empty") with `clz`/`ctz`, which are branch-free.

### 7.3 Cache behaviour

Again without hardware counters, but the profiles match. I would expect **A to have by far the most cache misses**, then B, with C the fewest.

- **A** follows pointers all the time and allocates lots of small objects: ~700k order nodes and thousands of level nodes, each a separate `malloc` placed wherever the allocator decides. A lookup is a chain of dependent loads (`node->left->right->...`). The CPU can't start the next load until the current one returns, so each cache miss (~100 ns to DRAM) is paid in full. The order tree is ~30+ MB, much bigger than the caches, so the deep levels of the tree almost always miss. That fits cancel being 719 ns.
- **B** jumps around memory but follows fewer pointers. A hash lookup is: compute the bucket, load the bucket array, follow one pointer to the node. That is about 2 misses instead of ~20. The `unordered_map` still allocates one node per order, though. The heap is a contiguous `vector<int>` (cache-friendly), but `get_bids`/`get_asks` copy the *whole* heap (thousands of ints) every 100 ops, which is a lot of memory traffic for 10 levels.
- **C** mostly reads contiguous memory. `qty[]` is 80 KB, so it stays in L2 and the hot part near the inside stays in L1. The bitmaps are 2.5 KB. The order vector is filled in id order, so adds write sequentially (the prefetcher handles it). Only the cancel of a random old order is a real random access, and that is a single load with no pointer chasing after it.

### 7.4 Page faults

**B has the most page faults (22,670)**, then A (10,594), then C (1,223). Each process runs 5 benchmark runs, so a fault is counted every time memory is newly touched.

- B uses the most memory. Every order is a hash node (~40 B incl. malloc header), plus the bucket array. `unordered_map` **rehashes** when it grows, which allocates a new, bigger bucket array and touches all of it. On top of that, `get_bids`/`get_asks` allocate a full copy of the heap 20,000 times per run. Lots of malloc/free means the allocator keeps getting fresh pages from the kernel.
- A allocates the same number of nodes but never has to reallocate a big array. The tree just adds nodes, so it takes fewer faults than B, but still a lot because of ~700k separate small allocations.
- C allocates a few big blocks once: the order vector (reserved for 1M entries, doubled if needed), and the fixed 80 KB array that is part of the object. The kernel can back big allocations with huge pages (2 MB), so a few large allocations cost very few faults. That shows up as 1,223, which is about 9× fewer than A.

So page faults are a good proxy for "how many separate allocations and how much memory does this design churn through".

### 7.5 Production choice

I would deploy **C**, but not just because it has the lowest average:

- **Latency:** lowest for every operation, and cancel (the slowest op for the others) is ~20× faster than A.
- **Predictability:** this matters more than the average in trading. C never allocates in the hot path (after the order vector has grown to its working size), never rehashes and never rebalances, so there are no rare big spikes. B looks fine on average, but it has hidden spikes: a rehash of the order map, or a `get_bids` that copies a heap full of stale ticks. A's latency depends on tree size and on where the allocator put the nodes. C's runs were also the most consistent: 40–43 ms total, against 96–107 ms for B and 264–494 ms for A (`results/bench_*.txt`).
- **Memory:** C's level storage is fixed (80 KB + 2.5 KB of bitmaps) no matter how many orders there are. The order vector is 24 B per id (`sizeof(Order)`), which is bigger than needed if ids are sparse. That is why ids ≥ 2²⁴ go to a hash map. In production I'd recycle slots with a free list instead of using the exchange id directly.
- **Cache behaviour:** small, contiguous, no pointer chasing (see 7.3).
- **Complexity / maintainability:** C is the hardest to read. The bitmap `prev`/`next` code with `clz`/`ctz` and masks is easy to get off by one, and A is ~60 lines that anyone can check. I'd handle this with the `--verify` lockstep test: A is simple enough to trust, so it is the reference model, and C is checked against it on millions of random ops including the narrow/tiny books that hit the edge cases. C also only works because the price range is small and fixed (10,000 ticks). For an instrument with an unbounded price range it would need a sliding window around the mid.

B is the worst of both. It is more complex than A (lazy deletion, duplicates, `mutable` heaps), but still allocates per order and has spiky depth queries. I would not pick it.

### 7.6 What I would try next

1. **Remove `lround`**. It shows up as `lround@plt`, an actual library call on every add. With a fixed 0.01 tick, `(int)(price * 100.0 + 0.5)` is enough, or better, take integer ticks in the API.
2. **Object pool / free list for order slots**, so the order table stays dense even with sparse or huge exchange ids, and memory gets reused after cancels.
3. **Smaller `Order`** (pack tick as `uint16_t` offset, qty as `int32_t`, side in a bit) to fit 8 B per order instead of 24, so three times as many orders per cache line, which helps the random cancels.
4. **Pre-fault / `mlock` memory at startup**, so the page faults happen before trading starts and not on the first orders.
5. **Prefetching** for cancels: the order slot could be prefetched as soon as the cancel message arrives, before the book needs it.
6. For B, use the bitmap (or a `std::set`) instead of a heap for depth queries, so it doesn't copy the heap. For A, try `std::map` with a pool allocator to see how much of A's cost is `malloc` vs pointer chasing.
7. Measure **latency percentiles (p50/p99/p99.9)** instead of only the mean, and get the hardware counters (IPC, cache and branch misses) on a lab Linux machine to confirm sections 7.2 and 7.3 with real numbers.
