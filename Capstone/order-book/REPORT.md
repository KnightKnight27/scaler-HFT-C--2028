# Performance Report: Three Order Book Designs

Anuska Roy · anuska.24bcs10021@sst.scaler.com · Roll no. 10021

## Environment and method

| | |
|---|---|
| Machine | Apple M1 MacBook (8 cores), macOS 14.8.1 |
| Native compiler | Apple clang 15.0.0, `-std=c++17 -O2` (libc++) |
| Linux (perf) | Docker Desktop VM: Debian bookworm, kernel 7.0.14-linuxkit aarch64, g++ 12.2, perf 6.1 |
| Workload | 1,000,000 ops, seed 42: 700,829 add / 199,496 cancel / 99,675 BBO, plus `get_bids(10)` + `get_asks(10)` every 100 ops |

**Where each number comes from.** I don't have access to a Linux lab machine, so I
used several sources and say which one each number comes from:

1. **Latency (ns/op, total ms)** is measured **natively on macOS**. Each implementation
   runs 5 times on a fresh book and the table shows the **median**.
2. **`perf stat` / `perf record`** were run on Linux inside Docker. On Apple Silicon the
   Docker VM does **not expose hardware PMU counters**. `cycles`, `instructions`,
   `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and
   `dTLB-load-misses` all report `<not supported>`. Only the software counters
   **`task-clock` and `page-faults` work**. For `perf record` I sampled with the
   `cpu-clock` software timer instead of `cycles`.
3. **IPC** comes from macOS's own hardware counters (`/usr/bin/time -l` reports
   "instructions retired" and "cycles elapsed").
4. **Branch-miss and cache-miss rows** come from **Valgrind cachegrind**. It is a
   *simulated* L1/last-level cache and branch predictor, so these are not hardware
   counts. They are good for comparing A, B and C, but not as absolute numbers.

Whole-process counters (3, 4) include generating the workload. I subtract a baseline
run (`--impl 0`, which only generates the workload) to get book-only numbers. All
counter runs use `--untimed`, so the ~18 ns of `clock_gettime` around each op is not
counted. All raw output is in [`results/`](results/).

## Results table

| Metric | A: `std::map` + `std::list` | B: heap, lazy delete | C: array + bitmap + pool |
|---|---:|---:|---:|
| Total time (ms), per-op timers on | 248.2 | 145.6 | **75.7** |
| Total time (ms), `--untimed` | 226.0 | 102.0 | **37.5** |
| ns / add | 199.6 | 84.5 | **56.0** |
| ns / cancel | 414.5 | 235.3 | **77.6** |
| ns / BBO ¹ | 18.5 | 20.2 | 17.4 |
| ns / depth query (bids+asks, 10 levels) | 166 | 1,784 | **102** |
| Instructions per run, book only (M1 HW) | 1,841 M | 1,146 M | **216 M** |
| IPC (M1 HW, book only) | 1.67 | **2.81** | 1.66 |
| Branches, book only (cachegrind) | 121.8 M | 84.9 M | **23.6 M** |
| Branch mispredicts (cachegrind) | 8.37 M (**6.9 %**) | 2.72 M (**3.2 %**) | 1.49 M (6.3 %) |
| L1-dcache misses (cachegrind, D1) | 17.7 M | 23.0 M | **5.8 M** |
| Cache misses, last level (cachegrind, LLd) | 11.0 M | 7.5 M | **5.1 M** |
| dTLB-load-misses | unavailable | unavailable | unavailable |
| Page faults (Linux `perf stat -r 5`, per process ²) | 15,943 | **9,492** | 47,880 |
| Page reclaims (macOS, per process ²) | 17,314 | 12,290 | **7,864** |
| Peak memory, book only (macOS footprint) | 48.7 MB | 32.2 MB | **29.6 MB** |

¹ `get_bbo` is O(1) in every design and costs a few nanoseconds. The measured value is
almost all the cost of the two `steady_clock::now()` calls around it, which is
17–18 ns on this machine (printed by the benchmark). The three are not
distinguishable at this resolution.
² A process runs 5 fresh books (`--runs 5`). Baseline (workload generation only):
1,747 Linux faults, 2,630 macOS reclaims.

Linux timings inside the VM rank the designs the same way (median total: A 1225 ms,
B 435 ms, C 225 ms; see `results/linux/bench_*.txt`). They are 2–5× slower and much
noisier than native, so I use the native numbers above.

**`perf record` (Linux, `--untimed`) hot spots**, from `results/linux/perf_report_*.txt`:

- **A:** `cancel_order` 17.1 %, `add_order` 16.0 %, `map` insert 13.7 %, **`free` 13.1 %**,
  kernel `clear_page` 6.4 %, `unordered_map` rehash 3.4 %.
- **B:** `cancel_order` 13.1 %, `unordered_map<id,…>` 7.0 %, `clear_page` 6.4 %,
  `add_order` 5.9 %, `free` 5.8 %, `malloc` + `operator new` 6.6 %.
- **C:** `add_order` 26.7 %, **`clear_page` 15.2 %**, `main` (benchmark loop with
  `get_bbo`/depth inlined) 13.1 %, workload generation + `mt19937` 19.8 %,
  `cancel_order` 6.6 %.

In C the book itself is now so cheap that workload generation is a large part of the
profile.

## Analysis

### 11.1 Which implementation is fastest?

**C is fastest for `add_order` (56 ns) and `cancel_order` (78 ns)**, and for the depth
query. **`get_bbo` is a tie**: all three keep the best price at a known place
(`map.begin()`, `heap.top()`, the cached `best_bid_` index), so it is one or two loads
from memory that is already in cache.

C is fast mainly because it **does much less work**. It executes 8.5× fewer
instructions than A and 5.3× fewer than B, and its IPC (1.66) is no better than A's.
Where that work goes:
- **No allocator calls.** A calls `malloc` twice per add (list node + hash node) and
  `free` twice per cancel; `free` alone is 13 % of A's profile. B allocates one hash
  node per order. C writes into a pre-sized vector slot and recycles freed slots
  through a free list.
- **Locating a price level is array indexing**: `levels_[tick - 5000]`. In A it is a
  tree descent of about 12 levels on every add and on every cancel (`map.find`),
  and every level is a dependent load to a node that may be anywhere in memory.
  B pays two hash lookups (order + level).
- **The id index is open addressing**: one hashed probe into a flat array, with
  linear probing that stays within a cache line or two. `std::unordered_map` (A, B)
  goes bucket → node pointer → node.
- **Cancels in C** are a hash probe, an unlink of the order from its doubly linked
  list (touching two neighbours in the pool), and an array update. The best-price
  rescan happens only when the best level empties. With about 50 orders per level
  in the steady state (≈ 501 k live orders over ≈ 10 k levels) that is rare, and when it does happen it checks 64 levels per
  instruction.

B beats A because, once all ~5,000 price levels exist, almost every `add` is two hash
operations (`try_emplace` finds an existing level and nothing is pushed on the heap).
The heap is only touched when a level is created or a stale top is pruned. B's weak
spot is `get_bids`/`get_asks`: a heap is not sorted, so it copies the whole heap
(~5,000 ticks, 20 KB) and pops. That is 1.8 µs against 0.1–0.17 µs for A and C, and it
shows up as B's high L1 **write** miss count (7.5 M of B's 23 M D1 misses are writes).

### 11.2 Branch prediction

**A has the most mispredictions**, both absolute (8.4 M) and as a rate (6.9 %).
Inserting a random price into a red-black tree means about 12 key comparisons, each
going left or right depending on a random number. The predictor cannot learn that
pattern, so roughly half of these branches miss. `map.find` in every cancel repeats
the same walk. Rebalancing (colour checks, rotations) and walking hash bucket chains
add more data-dependent branches.

**C's rate is similar (6.3 %), but it executes 5× fewer branches, so it has 5.6× fewer
misses.** Its unpredictable branches are the length of the linear-probe loop, "is this
level empty?" (`total == 0`), "is the list empty?" (`tail == kNil`) and "is the new
price better than the best?". All of them depend on random prices and ids.

**B has the lowest rate (3.2 %)**. In steady state its hot path is two hash lookups
whose branches mostly go the same way. A new-level heap push and a lazy-deletion
prune are both rare, and a sift-up of a random price usually stops after one or two
steps.

### 11.3 Cache behaviour

**A has the most last-level misses (11.0 M); B has the most L1 misses (23.0 M).**

- **A follows pointers through many small objects.** Every order is a `std::list`
  node *and* an `unordered_map` node, and every level is a red-black tree node. All of
  these are separate allocations spread over about 49 MB. A cancel goes hash bucket →
  hash node → tree root → … → tree node → list node → its neighbours. That is a chain
  of dependent loads, and most of them land on lines that are not cached. That is
  why A has the most LL misses and why its IPC is limited.
- **B** keeps the heap itself contiguous (`std::vector<int>`). But orders and levels
  live in node-based `unordered_map`s (pointer chasing again), and the depth query
  streams a 20 KB copy of the heap through L1 10,000 times per run.
- **C** keeps everything that is touched constantly contiguous and small: the level
  array is 10,001 × 16 B = 160 KB, and the bitmap is 157 words. The levels near the
  top of the book, which are the most active, sit next to each other and stay in
  cache. The remaining misses come from the order pool (16 MB) and the id index
  (1 M slots × 16 B = 16 MB at its peak). Ids hash to random slots, so most index probes miss. This is
  why `add_order` is the largest item in C's profile.

### 11.4 Page faults

The two platforms disagree. The reason is how the allocator returns memory, not how
much memory each book uses.

- **On Linux, C has by far the most faults (47.9 k per 5-book process).** C's pool and
  index are large `std::vector`s that grow by doubling. Every growth allocates a new,
  larger block, which glibc serves with a fresh `mmap`. The kernel zero-fills every new
  page on first touch (`clear_page` is 15 % of C's profile). The old block is copied
  and unmapped. When the book is destroyed everything is unmapped, so the next book
  faults all of it back in. I checked this directly
  (`results/linux/page_faults_runs.txt`): each extra book in the same process costs
  C about **9.3 k** faults, but only about **0.5 k** for A and **0.35 k** for B.
  A's and B's small nodes come from glibc's heap arena. Freed nodes stay in the
  arena and are reused by the next book, so only the first book pays for the faults.
- **On macOS, C has the fewest reclaims (7.9 k vs 17.3 k for A).** macOS uses 16 KB
  pages, which means 4× fewer faults for the same bytes. Large allocations behave
  differently there, and C's total memory footprint is the smallest (29.6 MB vs 48.7 MB
  for A). A uses the most memory per order: about 100 B in two malloc'd nodes plus a
  bucket slot, against 32 B pool + 32 B index in C.

So C's memory use is the lowest, but its allocation pattern (a few huge blocks that
grow and get thrown away) causes many page faults on Linux. For a trading system
that is easy to fix: build the book once at startup and pre-fault it. Pre-sizing alone
is not enough. I tried a scratch build of C with the pool and index reserved for
1 M orders. Single-book faults dropped from 25.1 k to 12.3 k, but 5 books still caused
62.9 k faults, because every new book touches its memory again. Latency did not
change measurably (79 ms vs 76 ms).

### 11.5 Production choice

**I would deploy C**, with two hardening steps (allocate everything at startup and
pre-fault it; see 11.6). Reasons:

- **Latency:** lowest for add, cancel and depth, and BBO is as cheap as it can be.
- **Predictability:** no allocator calls on the hot path once the pool is warm. A and
  B call `malloc`/`free` for every order, and allocator time varies (lock-free fast
  paths vs. occasional trips to the OS). C's worst cases are bounded and visible in
  the code: a bitmap scan of at most 157 words when the best level empties, and the
  growth events, which pre-allocation removes entirely. A's tree rebalancing and B's
  1.8 µs depth query are the latency spikes I would least want.
- **Memory:** lowest (≈ 30 MB for ~500 k live orders vs 49 MB for A). The 160 KB level
  array is a fixed cost and fits in L2.
- **Cache behaviour:** contiguous arrays, 32-bit indices instead of 64-bit pointers,
  and the hot levels stay in cache.
- **Complexity / maintainability:** this is C's real cost. A is about 100 lines of
  obvious standard-library code that anyone can review. C has a hand-written hash
  table (backward-shift deletion is easy to get subtly wrong), index-linked lists and
  bit tricks. It also hard-codes the price band: a wider band would need a bigger
  array or a sliding window. I would accept that because the verifier
  (cross-checking against a simple reference after every operation, on three
  workloads) catches regressions, and the price band of one instrument is known in
  advance.

I would not pick B, even though it beats A. Its depth query is slow, and the lazy
deletion bookkeeping adds complexity without giving C's speed.

### 11.6 What would I try next?

1. **Allocate everything at startup and pre-fault it** (pool and index sized for the
   worst case and touched once at startup, possibly with `mlock`/huge pages). Then no
   faults or growth spikes can happen during trading. The 11.4 experiment shows that
   reserving alone is not enough; the memory must be touched up front, and the book
   should be reused rather than rebuilt.
2. **A better id index for this id pattern.** The exchange assigns ids in increasing
   order, so a direct `id - base` → slot ring buffer (falling back to the hash table
   for ids outside the window) would make lookups sequential instead of random. That
   attacks C's biggest remaining cost (hash-index misses in `add_order`).
3. **Hot/cold split of the order record.** Cancel only needs `{qty, prev, next, level}`
   (16 B), not the 64-bit id. Smaller records fit more per cache line.
4. **A two-level bitmap** (a summary word that marks which of the 157 words are
   non-zero) so the worst-case best-price rescan is 3 instructions instead of up to
   157 words.
5. **Fix A cheaply:** store the `map` iterator in the locator so cancel skips
   `map.find`, and give `std::list`/`std::unordered_map` a pool allocator. That would
   show how much of A's cost is the tree and how much is `malloc`.
6. **Remove B's depth problem:** keep a small sorted cache of the top-N levels next to
   the heap, refreshed when the top changes.
7. **Measure tail latency, not only averages:** record per-op latency histograms
   (p50/p99/p99.9) with `rdtsc`/`cntvct_el0`, and get real hardware counters
   (`branch-misses`, `L1-dcache-load-misses`, `dTLB-load-misses`) on a bare-metal Linux
   machine using `scripts/run_perf.sh`.

## Appendix: raw `perf stat` output (Linux / Docker)

`perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X`
(`--untimed` variants are in `results/linux/perf_stat_*_untimed.txt`.)

```
 Performance counter stats for './bench --impl A' (5 runs):

           2717.36 msec task-clock                       #    0.712 CPUs utilized            ( +- 19.33% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             15943      page-faults                      #    4.248 K/sec                    ( +-  1.17% )

             3.817 +- 0.559 seconds time elapsed  ( +- 14.66% )
```

```
 Performance counter stats for './bench --impl B' (5 runs):

           1424.01 msec task-clock                       #    0.756 CPUs utilized            ( +- 16.74% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              9492      page-faults                      #    5.234 K/sec                    ( +-  5.02% )

             1.882 +- 0.290 seconds time elapsed  ( +- 15.42% )
```

```
 Performance counter stats for './bench --impl C' (5 runs):

           2129.21 msec task-clock                       #    1.260 CPUs utilized            ( +-  6.74% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             47880      page-faults                      #   28.522 K/sec                    ( +-  8.08% )

             1.690 +- 0.150 seconds time elapsed  ( +-  8.87% )
```

For B and C, most of the timed `task-clock` is per-op `clock_gettime` calls,
which are slow in this VM. Without them (`--untimed`) it drops to B 764 ms and
C 317 ms per 5-book process. A stays at 2827 ms, the same as timed within the
±19 % VM noise, because A's own work is much larger than the timer cost.
