# Performance Analysis Report

## 1. Designs

### Implementation A: `std::map` + `std::unordered_map`
- Orders: `unordered_map<OrderId, Order{side, price, qty}>`, one heap node per order.
- Price levels: `std::map<Tick, Qty>` per side (bids use `std::greater` so the best bid is first), one tree node per level.
- Best price: `begin()` of each map.
- Cancel lookup: hash lookup by id, then the level is found in the map. This currently takes three tree lookups: two `operator[]` calls and one `erase(key)`.

### Implementation B: sorted vector + `std::unordered_map`
- Orders: same hash map as A.
- Price levels: `std::vector<Level>` per side, kept sorted with the best price at `back()` (bids ascending, asks descending).
- Best price: `back()`.
- Cancel lookup: hash lookup by id, then `std::lower_bound` to find the level; the level is erased if its quantity reaches zero.

### Implementation C: flat arrays (own design)
- Orders: `std::vector<Order>` indexed directly by order id. A cancelled order is marked with `q = 0`.
- Price levels: one `std::array<Qty, 10001>` covering ticks 5000 to 15000 (about 80 KB), indexed by `tick - 5000`.
- Best price: cached `bb_px_` / `ba_px_`, updated in O(1) on add. When the best level empties, a linear scan finds the next non-empty tick.
- Cancel lookup: `ord_[id]`, no hashing.

A and B share the order storage and differ in level storage and best-price lookup. C differs from both in all four aspects.

### Complexity

L = price levels on one side (at most 5,000), k = requested depth.

| Operation | A | B | C |
|---|---|---|---|
| add_order | O(log L) + O(1) avg hash | O(log L) search, plus O(L) shift if the level is new | O(1) amortised (vector may reallocate) |
| cancel_order | O(log L) + O(1) avg hash | O(log L), plus O(L) shift if the level empties | O(1), or O(range) rescan if the best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids / get_asks | O(k) | O(k) | O(k + empty ticks skipped) |

## 2. Results

Environment: GitHub Codespaces, x86_64, 2 vCPU, Linux 6.8 (Azure), g++ 13.3, `-O2`. Timings are medians of 5 runs (`results/bench_*.txt`). Page faults come from `perf stat -r 5` (`results/perf_stat_*.txt`) and are counted per process, where one process does 5 benchmark runs. Peak RSS comes from `/usr/bin/time -v` with `--runs 1` (`results/memory_usage.txt`).

| Metric | A (map) | B (sorted vector) | C (flat array) |
|---|---|---|---|
| Total time per run (ms) | 335.5 | 237.1 | 114.6 |
| ns / add | 260.8 | 173.3 | 74.8 |
| ns / cancel | 594.5 | 408.2 | 126.8 |
| ns / BBO | 34.5 | 32.6 | 32.0 |
| IPC | not supported | not supported | not supported |
| Branch misses (%) | not supported | not supported | not supported |
| L1-dcache-load-misses | not supported | not supported | not supported |
| Cache misses | not supported | not supported | not supported |
| Page faults | 16,715 | 16,623 | 57,387 |
| Peak RSS (MB) | 54.8 | 54.3 | 56.3 |

Unavailable counters: `cycles`, `instructions`, `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and `dTLB-load-misses` all report `<not supported>`, because the Azure VM does not expose the hardware PMU to the guest. I had to lower `perf_event_paranoid` to -1 (and `kptr_restrict` to 0 for kernel symbols) for perf to work at all. So IPC, branch and cache behaviour below are reasoned from the code and from `perf record`, not measured directly.

"Total time" is the `total_ms` printed by the benchmark (the op loop only). I did not use perf's task-clock for this because it also includes workload generation, which is 8 to 24% of samples in the profiles.

All runs print the same final BBO (bid 99.99 x 2668, ask 100.01 x 2486) and checksum 483587156, and `--verify` passes for all three (`results/verify.txt`).

### Where time goes (`perf record`, `results/perf_report_*.txt`)

Approximate shares of all samples, grouped from the symbolized profiles:

| | A | B | C |
|---|---|---|---|
| Order book code | ~51% (`run_once` with inlined add 26.8%, `cancel_order` 24.2%) | ~41% (`cancel_order` 20.4%, `add_order` 15.4%, `run_once` 4.8%) | ~14% (all inlined into `run_once`) |
| `unordered_map` (lookup + rehash) | ~8.6% (`operator[]` 4.4%, `_M_rehash` 4.2%) | ~10% (`operator[]` 5.0%, `_M_rehash` 5.1%) | none |
| malloc/free on the hot path | ~3.5% | ~6% (incl. `malloc_consolidate` from `cfree` on cancel) | ~1% (vector growth only) |
| `memmove` | ~0% | 1.95% (level inserts in `add_order`) | 2.03% (`ord_` reallocation) |
| `clock_gettime` / `steady_clock::now` | ~13% | ~16% | ~35% |
| Kernel page-fault / munmap | ~1–2% | ~2% | ~10% |
| Workload generation (untimed) | ~8% | ~8.5% | ~24% |

## 3. Analysis

### 3.1 Which implementation is fastest?

C is fastest for add (74.8 ns, 3.5x faster than A) and cancel (126.8 ns, 4.7x faster than A). B is in between, about 1.5x faster than A for both operations.

For BBO all three are effectively the same (32 to 35 ns). get_bbo is O(1) in every design, so this number is mostly the cost of the two `steady_clock::now()` calls around it. That is the timer floor of my benchmark, and every other per-op number includes it as well. In C's profile the clock accounts for about a third of all samples, more than twice the time spent in the order book code itself (about 14%). So C's real add and cancel costs are well below the measured numbers, and the measured gap to A understates the real difference.

Why C is fastest: an add is one array increment plus a write to `ord_[id]`, and a cancel is one indexed read plus one array decrement. There is no hashing, no tree walk and no per-order allocation.

Why B beats A: both use the same hash map for orders, but B finds a level by binary search over a contiguous array, while A walks tree nodes spread around the heap. B's `memmove` is only about 2% of its profile, which surprised me. With 1M operations, roughly 500k orders stay live (my estimate from the 70/20 add/cancel mix) over 5,000 ticks per side, so almost every level becomes non-empty early in the run and very rarely empties again. B therefore almost never inserts or erases a level, and its cost is the binary search plus the hash map.

Cancel is slower than add in all three. An add writes to memory that was just allocated or is next in sequence. A cancel picks a random live order out of hundreds of thousands, so the lookup (hash node in A and B, `ord_[id]` in C) usually lands on memory that is no longer in cache. A also does three tree lookups in `cancel_order` (`bids_[p]` twice, then `erase(p)`), which matches `BookMap::cancel_order` being the largest single function in A's profile at 24%.

### 3.2 Branch prediction

I could not measure branch misses on this VM, so this section is a prediction from the code.

I expect A to have the most branch misses. Every map lookup walks a red-black tree, and the direction taken at each node depends on comparing random prices, which is close to unpredictable. A does this up to three times per cancel. B's `lower_bound` has the same kind of data-dependent branch, but only about 12 to 13 comparisons over a contiguous array per operation. Both A and B also walk hash buckets, and B's frees on cancel trigger `malloc_consolidate`, which walks free lists. Both of those are data-dependent too.

C's hot path is mostly straight-line arithmetic. Its branches (buy or sell, new best price or not, does `ord_` need to grow) are fairly predictable, and the rescan loop only runs when the best level empties, which rarely happens in this workload.

### 3.3 Cache behaviour

There are no cache counters, so this is reasoned from the structures and the profiles.

- **A** follows pointers everywhere: map nodes for levels and hash nodes for orders, each a separate small heap allocation. Every add allocates a hash node (`_int_malloc` called from `unordered_map::operator[]`, about 1.8%) and every cancel frees one. The hash table also rehashes as it grows (`_M_rehash`, 4.2%) because it is never reserved, and each rehash touches every node. `_Rb_tree_insert_and_rebalance` does not appear in the profile and `_Rb_tree_rebalance_for_erase` is only 0.05%, which confirms that map nodes are almost never created or destroyed once the book fills up.
- **B** keeps levels contiguous, so finding a level is cache-friendly, but orders are still hash nodes, so cancels still chase pointers. It has the same rehash cost as A (`_M_rehash`, 5.1%).
- **C**'s level array is about 80 KB, contiguous, and fits in L2. Orders are a contiguous array indexed by id. The only cache-unfriendly access is the random `ord_[id]` read on cancel. No pointers are followed and there are no per-order allocations.

A note on the profiles: `malloc_consolidate` and `unlink_chunk` also appear in A's profile (about 3%), but the call graph shows they come from `printf` after the run, when malloc merges the ~500k hash nodes freed by the book's destructor. That is teardown cost, not per-operation cost.

### 3.4 Page faults

C has about 3.4x more page faults (57,387 vs about 16,700), even though its peak memory is almost the same as A and B (56.3 MB vs 54.8 MB). I tested why by changing the number of runs inside one process (`results/pagefault_experiment.txt`):

| | runs=1 | runs=5 | extra faults per additional run |
|---|---|---|---|
| A | 15,323 | 16,715 | ~350 |
| C | 18,188 | 57,390 | ~9,800 (~38 MB) |

A frees its small hash nodes at the end of a run and malloc keeps that memory, so the next run reuses it and barely faults. C's `ord_` vector grows with `resize(id + 1)` and keeps reallocating (each Order is 24 bytes and there are about 700k ids). Those large buffers are returned to the OS when freed, so each run faults in fresh zeroed pages again.

The call graph shows this directly. In C's profile, page faults are triggered from `memmove` and `vector<Order>::_M_default_append` (the resize copying `ord_`), and `__munmap` → `zap_pte_range` shows the old buffer being handed back to the OS. Kernel page-fault and unmap work is roughly 10% of C's samples.

So page faults here measure how often a program touches new pages, not how much memory it holds. It also means C does allocate on the hot path: an add that triggers a reallocation copies the whole vector. Calling `ord_.reserve()` up front would remove both the faults and the copy spikes.

### 3.5 Production choice

I would deploy C, but only with the changes listed below.

- **Latency:** lowest for add and cancel by a wide margin.
- **Predictability:** across the five `perf stat` runs, C's median cancel stayed between 68 and 113 ns, while B's ranged from 215 to 425 ns and A's from 565 to 632 ns. Some of this is noise from a shared 2-vCPU VM, but A and B also depend on malloc state, rehashing and cache misses from pointer chasing, which all vary more. C still has two sources of spikes: vector reallocation (fixed with `reserve` or a fixed-size pool) and the linear rescan when the best level empties, which in a thin book could scan thousands of ticks.
- **Memory:** the level array is fixed at 80 KB. However, `ord_` grows with the largest order id ever seen, not with the number of live orders, and cancelled slots are never reused. In production, with ids that keep increasing all day or are sparse, this has to become a pool with a free list plus an id-to-slot map.
- **Cache behaviour:** contiguous arrays and no pointer chasing, the best of the three.
- **Complexity and maintainability:** A is the simplest and the hardest to get wrong, which is why I treat it as the reference in `--verify`. C has more invariants to keep correct (cached best prices, the rescan, the fixed price band). The equivalence test against A over 1M operations, plus the edge-case tests, is what makes me comfortable using C.

### 3.6 What I would try next

1. **Better timing.** Per-op `steady_clock` calls dominate C's numbers. I would time batches of operations or use `rdtsc`, and report percentiles (p50/p99) instead of only the average.
2. **Reserve or pool C's order storage** to remove the reallocations, copies and page faults.
3. **Replace C's linear rescan with a bitmap** of non-empty ticks (10,001 bits is 157 64-bit words), finding the next best level with `std::countr_zero` / `std::countl_zero` (C++20) or the GCC builtins.
4. **Fix A's cancel** to use one `find()` iterator instead of three tree lookups.
5. **Call `orders_.reserve()` in A and B.** `_M_rehash` is 4 to 5% of their profiles and is pure growth cost.
6. **Test a thin-book workload** (prices clustered near the mid, more cancels). That would stress B's shifting and C's rescan, which this saturated workload barely exercises.
7. **Run on bare-metal Linux** to get IPC, branch-miss and cache-miss counters that confirm or disprove sections 3.2 and 3.3.