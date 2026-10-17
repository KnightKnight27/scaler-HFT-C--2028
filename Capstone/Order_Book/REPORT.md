# Report: Low-Latency Order Book

Shreyash Sukahdev Kawde, roll no. 10253

Machine: Intel Core i5-1335U (2 P-cores + 8 E-cores), Ubuntu Linux, g++ 15.2 with `-O2`.
Workload: 1,000,000 operations (700,829 add, 199,496 cancel, 99,675 BBO) plus `get_bids(10)` and `get_asks(10)` every 100 ops, `mt19937` seed 42.
Everything is pinned to P-core 0 with `taskset -c 0`. Raw outputs are in `results/` and the screenshots are in `screenshots/`.

## 1. Correctness

`./bench --verify` runs the same workload through A, B and C and compares them after every operation (return values, BBO, top 10 levels every 100 ops, the whole book every 5000 ops). It also tries to cancel already-cancelled ids (39,899 times), runs edge cases (empty book, unknown cancel, short depth, bad prices, duplicate ids), and checks A against a slow naive reference for the first 50,000 ops.

```
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

(`results/correctness.txt`, `screenshots/01_build_and_verify.png`). To make sure the test can fail, I broke B and C on purpose in a copy of the code and it reported FAIL at op 15.
The checksum printed by the benchmark is also identical for all three (`17175048581270136155`), and the final BBO is `99.99 x 1608` / `100.01 x 2808`.

## 2. Results table

Median of 5 runs for the time numbers. The perf numbers come from `perf stat -r 5` (one workload replay per run, `--total-only`).

| Metric | Implementation A | Implementation B | Implementation C |
| ----- | ----- | ----- | ----- |
| Total time (ms) | 371.5 | 430.5 | 57.5 |
| ns / add | 336.6 | 274.4 | 237.9 |
| ns / cancel | 662.9 | 1210.8 | 240.3 |
| ns / BBO | 21.1 | 16.6 | 32.9 |
| IPC | 0.83 | 0.95 | 1.00 |
| Branch misses (%) | 6.88 | 6.60 | 4.25 |
| L1-dcache-load-misses | 13,515,984 | 18,666,260 | 3,592,159 |
| Cache misses | 6,173,662 | 7,225,460 | 4,851,683 |
| Page faults | 15,406 | 15,856 | 14,159 |

Other counters from `perf stat`:

| | A | B | C | only building the workload |
| ----- | ----- | ----- | ----- | ----- |
| instructions | 736.5 M | 1004.7 M | 385.2 M | 220.0 M |
| branches | 132.0 M | 196.1 M | 48.3 M | 20.6 M |
| branch-misses | 9.09 M | 12.94 M | 2.05 M | 0.81 M |
| dTLB-load-misses | 0.66 M | 0.88 M | 0.68 M | 0.01 M |
| page-faults | 15,406 | 15,856 | 14,159 | 8,011 |

Also, ns per `get_bids(10)` + `get_asks(10)`: A 374.9, B 291.5, C 347.0.

**Counter availability.** This CPU is hybrid (P-cores and E-cores), so `perf` lists every event twice, as `cpu_core/...` and `cpu_atom/...`. I pinned the program to a P-core, so all `cpu_atom/...` lines show `<not counted>`, and `cpu_atom/L1-dcache-load-misses` shows `<not supported>`. All 9 requested counters worked on the P-core and the table uses those. `perf stat` was run as `perf stat -r 5 -e ... ./bench --impl X --runs 1 --total-only` (one replay per run instead of the benchmark's own 5 runs, and without the per-op timing pass, so the clock calls do not pollute the counters).

**How to read these numbers (important)**

1. *Two different kinds of time.* "Total time" is one timer around the whole loop. The "ns / op" numbers put `steady_clock::now()` around every call. Reading the clock waits for earlier loads to finish, so the CPU cannot overlap the cache misses of neighbouring operations any more. The per-op numbers are therefore closer to *latency* of one operation, and total time is *throughput*. This is why C's total is 57 ns per op on average while its add and cancel take about 240 ns each when timed alone: in the normal loop the CPU overlaps the memory accesses of many operations. I subtract the cost of a timer call (about 25 ns, measured at start-up), but that does not remove this overlap effect.
2. *BBO numbers are near the noise floor.* A `get_bbo` is only a few ns, the timer overhead is 25 ns, so the 16 to 33 ns values are mostly noise from the subtraction. I would not draw conclusions from the order of A, B and C for BBO.
3. *perf counts the whole process.* `perf stat` also counts creating the 1M operation list (the last column above, `--gen-only`) and freeing the book at the end. This matters most for C because it is so fast: more than half of C's instructions (220 M of 385 M) and more than half of its page faults (8,011 of 14,159) come from setup, not from the order book. I did not subtract it from the table, but I use it when explaining below.
4. *The machine is noisy.* This is a laptop with the `powersave` governor. Instruction, branch and miss counts repeat within 1 to 2%, but cycles and time moved by up to 30% between my sessions (for example C's `perf stat` cycles were about 250 M in an earlier run and 384 M in the run shown, with identical instructions). So IPC is only a rough number. The ranking A/B/C never changed.

## 3. Performance analysis

### 3.0 Where the time goes (`perf record -g` + `perf report`)

Flat profile, percent of samples (`results/perf_report_*.txt`, screenshots 08 to 13). The workload generator (`make_workload`) is part of every profile since it runs in the same process.

| | top entries |
| ----- | ----- |
| A | `run_once` 30% (this is `add_order` inlined: hash insert plus tree insert), `cancel_order` 27.5%, hashtable find/insert code 11%, `malloc`/`free` internals 7%, `make_workload` 6% |
| B | `cancel_order` 35.8%, `add_order` 14.6%, red-black tree insert/erase/rebalance about 11.5%, `malloc`/`free` 7%, `memmove` (vector shifting) 1.8% |
| C | `make_workload` + RNG 26.6%, `OrderTable::insert` 22.9%, `cancel_order` 7%, kernel page-fault and memory-zeroing functions about 10% |

Reading it: in A and B almost all the time is in cancel and add, and the cost sits in the id lookup structures (hash buckets, tree nodes), not in the price levels. In B `cancel_order` alone is more than a third of the run. In C the order-book work is small enough that generating the workload is the biggest single item. Inside C, the hash-table insert is the largest part: it writes to a random slot of a 24 MB table, so it is the one place that misses the cache. `memmove` in B is under 2%, so shifting the level vector turned out not to matter with 5,000 levels per side. C's profile only has about 540 samples (it runs for ~0.15 s), so its percentages are rough.

### 3.1 Which implementation is fastest?

C, by a big margin: 57 ms in total against 371 ms (A) and 430 ms (B), so 6 to 7.5 times faster.

- **add_order:** C is fastest (238 ns), then B (274 ns), then A (337 ns). B beats A for a slightly surprising reason: the workload uses ids 1, 2, 3, ..., so B's `std::map<id>` always inserts at the right end of the tree, which is already in cache. A's `unordered_map` sends every id to a random bucket, which is a cache miss.
- **cancel_order:** C (240 ns) is 2.8 times faster than A (663 ns) and 5 times faster than B (1211 ns). A cancel picks a random old id. In A that means a bucket miss, a node miss, then a tree lookup of the price level. In B it is a walk down a tree of 500,000 nodes (the top stays in cache, the bottom does not), a binary search of the levels, and a tree rebalance on erase (`_Rb_tree_rebalance_for_erase` alone is 5% of B's profile). C does one hash probe and one array update.
- **get_bbo:** all three are O(1). The measured 17 to 33 ns are inside the timer noise (see section 2), so I do not rank them.

### 3.2 Branch prediction

B has the most branch misses in absolute terms (12.9 M, against 9.1 M for A and 2.05 M for C). As a percentage A and B are close (6.9% and 6.6%) and C is the lowest (4.3%).
From the code: B's misses most likely come from the binary search over price levels (`std::lower_bound`), where each step goes left or right depending on a random price, and from walking the red-black tree, where every step is another random left/right choice. B does both searches on every operation, and it also executes the most branches (196 M). A has the same kind of unpredictable branches in its `std::map` price tree and in hash bucket chains of varying length. C has almost no data-dependent search: a hash probe usually ends on the first slot, and the bitmap search only runs when the best level empties. After removing the baseline (0.8 M), C has about 1.2 M misses for 1 M operations, and some of those come from the random 70/20/10 choice of operation type, which is in the workload for all three.

### 3.3 Cache behaviour

B has the most cache misses (7.2 M) and L1 misses (18.7 M), then A (6.2 M and 13.5 M), and C the fewest (4.9 M and 3.6 M).

- **A** follows pointers all the time. Hash nodes and tree nodes are separate small heap allocations made at different times, so they are scattered in memory, and each hop can miss. `malloc`/`free` internals are about 7% of A's profile.
- **B** is mixed. The level vector is contiguous (5,000 levels x 16 bytes = 80 KB per side), which is good for binary search and for reading the best levels. But the orders live in a tree with one allocated node per order, which is pointer chasing over a large structure. That explains the extra L1 misses compared to A (it also executes more instructions: 1005 M against 737 M).
- **C** has no pointers and no allocation in the loop: a flat table of orders (24 MB), an array of 15,001 quantities (120 KB, fits in L2) and a 2 KB bitmap. The one real miss is the first probe into the hash table, since ids hash to random slots. C has 3.8 to 5.2 times fewer L1 misses than A and B.
- dTLB misses are similar for all three (0.66 to 0.88 M). Each one touches tens of MB at random with 4 KB pages, including C's 24 MB table. Huge pages would probably help most there.

### 3.4 Page faults

B has the most (15,856), then A (15,406), then C (14,159). Just building the operation list costs 8,011 faults, so the books themselves add about 7.8 k (B, about 31 MB), 7.4 k (A, 29 MB) and 6.1 k (C, 24 MB).
A and B ask the OS for memory in small pieces as the book fills, because every order is a `malloc`. C allocates everything up front and zero-fills its 24 MB table in the constructor (about 6 k pages), so it has the same kind of faults, just all at the start and with no allocator work afterwards. The differences are under 10%, so page faults do not explain the speed gap. They do show that A and B use more memory per live order (node headers and allocator overhead) than C's fixed 24 bytes.

### 3.5 Production choice

I would deploy **C**, but not only because it is the fastest.

- **Latency and predictability:** C's work per operation is bounded: one hash probe plus array updates, and a bitmap search of at most 235 words plus 4 summary words. A and B depend on tree depth and heap layout, and call `malloc`/`free` on the hot path, which is a risk for tail latency (I did not measure tail latency, only averages). B also shifts memory when a level is inserted or erased. C's weak spot is `grow()`: past 50% load it rehashes everything at once. I start with 1 M slots, which is enough for the 500 k live orders here, but in production I would size the table for the maximum number of live orders up front.
- **Memory:** C is fixed and known (about 24 MB plus 120 KB). A and B grow with live orders plus allocator overhead.
- **Cache behaviour:** 4 to 5 times fewer L1 misses and no pointer chasing.
- **Complexity and maintainability:** this is where C loses. It is the most code (bitmap, custom hash table with backward-shift deletion) and the easiest to get wrong, since the cached best prices, the bitmap and the quantity array must always agree. It also depends on the fixed tick range, while A and B would not care if it changed. A is about 60 lines of plain standard library code. The `--verify` test (C against A and a naive model after every operation) is what makes C acceptable to me, and I would keep it running in the build.

### 3.6 What would I try next?

- Pre-size the hash table for the real maximum and never rehash on the hot path.
- Use an order pool addressed by a small integer handle (if ids can be made dense), which removes the hash miss on cancel.
- Keep a compact key table separate from the order data so probes touch fewer cache lines, and prefetch the slot of the next operation (the next op is known in this benchmark).
- Use 32-bit quantities and 16-bit tick offsets to shrink the structures (the level array would then fit in L1).
- Huge pages for the hash table to cut dTLB misses.
- Return depth results into a caller-provided buffer instead of a new `std::vector` each call.
- Measure each operation type without a clock read around every call (for example batch timing or `rdtsc`), to avoid the serialisation effect described in section 2.
- Repeat on a machine without hybrid cores and with the performance governor, for steadier cycle counts.
