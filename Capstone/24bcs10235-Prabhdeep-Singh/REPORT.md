# Performance Report: Low-Latency Order Book

Prabhdeep Singh, 24BCS10235, prabhdeep.24bcs10235@sst.scaler.com

## Setup

- **Benchmark machine:** Apple M4, macOS, `g++` is Apple clang 21 (libc++), `-O2`, single thread. Every number
  in the table comes from one run of each `./bench --impl X` (raw output in `results/bench_X.txt`).
- **Workload:** 1,000,000 ops, `mt19937(42)`, 70% add / 20% cancel / 10% BBO, `get_bids(10)` + `get_asks(10)`
  every 100 ops. The book peaks at 500,019 live orders and about 5,000 price levels per side. All three
  implementations see the identical workload and finish with the identical checksum.
- **perf:** macOS has no `perf`, so `perf stat -r 5` and `perf record -g` were run on Linux (a Linux VM on
  the same Mac, GNU g++ 12). That VM provides **only `task-clock` and `page-faults`**; `cycles`,
  `instructions`, `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and
  `dTLB-load-misses` are `<not supported>` (see `results/perf_stat_*.txt`), so those rows are unavailable
  below. The Mac and Linux runs use the same code, but `std::uniform_int_distribution` differs between libc++
  and libstdc++, so their workloads are the same kind, not byte-identical.
- **IPC** comes from the Apple M4's own counters (`instructions retired` / `cycles elapsed` printed by
  `/usr/bin/time -l` at the end of `results/bench_X.txt`). It covers the whole process: building the workload
  plus the 5 benchmark runs.

## Results table

| Metric | A: map | B: hash | C: array | Source |
|---|---|---|---|---|
| Total time (ms) | 112.1 | 1694.8 | 41.5 | `./bench --impl X`, Mac, median of 5, whole workload incl. depth queries |
| ns / add | 81.6 | 50.1 | 28.7 | same run |
| ns / cancel | 184.9 | 194.4 | 20.8 | same run |
| ns / BBO | 13.8 | 13.9 | 12.7 | same run |
| IPC | 1.82 | 1.55 | 2.43 | M4 counters: 5.26 G / 57.85 G / 2.61 G instructions retired |
| Branch misses (%) | n/a | n/a | n/a | `branch-misses` not supported on macOS or in the Linux VM |
| L1-dcache-load-misses | n/a | n/a | n/a | not supported |
| Cache misses | n/a | n/a | n/a | not supported |
| Page faults | 9,519 | 8,463 | 6,904 | Linux `perf stat -r 5 ... ./bench --impl X` (mean of 5, +-2-3%) |

`steady_clock` on this Mac advances in steps of about 42 ns and each per-op number includes one pair of
`now()` calls, so single measurements are coarse. The averages over 100k+ calls are meaningful, but the three
BBO figures are mostly timer and cannot be told apart.

## Analysis

### 11.1 Which implementation is fastest?

**C is fastest for `add_order` (28.7 ns vs 50.1 B vs 81.6 A) and for `cancel_order` (20.8 ns vs 184.9 A vs
194.4 B).** `get_bbo` is O(1) in all three and the 13-14 ns measured is mostly timer, so I cannot separate
them. For the whole workload C takes 41.5 ms, A 112.1 ms and B 1,694.8 ms.

Why: A's add walks a red-black tree about 12 levels deep (a pointer dereference per level) and goes through a
node-based hash map for the order. B's add is one hash lookup of the level plus the order insert. C's add is
one probe into a flat table plus one array write. For cancel, A and B both pay for an `unordered_map` find,
unlink and free of a node (a random cache line among ~500k nodes) and then a second lookup for the level;
C's single probe already yields the array index and quantity. The order table matters: in my first version
of C (same `std::unordered_map` for orders) cancel measured 64 ns; the flat table brought it to about 21 ns.
B is slowest overall because of the depth queries (11.3).

### 11.2 Branch prediction

The branch counters are not available on my machine, so I have no measured miss rates. What is measured: B
retires 57.9 G instructions against 5.3 G for A and 2.6 G for C (whole process), and `perf record` puts about
95% of B's samples in `get_bids`, `get_asks` and the two `std::sort` loops. From the code, **B is the one I
expect to mispredict most**: the comparisons inside `std::sort` depend on the order in which the hash table
returns its entries, which is effectively random, and the sort runs 20,000 times. A should be next, since each
step of a tree descent (left or right) depends on the random price being inserted. C should have the fewest:
its data-dependent branches are the probe loop's exit, the trip count of the cursor re-scan when the best level
empties, and the `idx > best_bid_` comparisons. This ranking is inferred from the code and the IPC, not measured.

### 11.3 Cache behaviour

Cache-miss counters are also unavailable, so this rests on the code, the IPC and the profile. **B should have
the most cache misses**: each depth query walks an `unordered_map` of ~5,000 separately allocated nodes
(pointer chasing, no locality), copies them into a vector and sorts it. `perf record` shows `get_asks` 30%,
`get_bids` 28% and the two introsort loops 37% of B's samples. A follows pointers (tree nodes, hash nodes) and
allocates one small object per price level and per live order; its profile is `add_order` 30%,
`cancel_order` 25% and allocator/hash-table internals about 10%, with a middling IPC (1.82). C is
contiguous: the 80 KB level array stays cache-resident, and each order operation touches one random slot in
the flat table (the one place C can miss), with no pointer to follow and no per-order allocation. It has the
highest IPC (2.43). One caveat from the literal `perf record -g ./bench --impl C`: 80% of C's samples are in
`__kernel_clock_gettime`, i.e. the per-op timers, and only about 11% are in C's own functions. In this
benchmark C's per-op times are therefore mostly timing overhead, and its real cost is lower than shown.

### 11.4 Page faults

**A has the most page faults (9,519), then B (8,463), and C the fewest (6,904, about 27% fewer than A).** Each
figure also includes building the workload, which is the same for all three. A and B allocate one separate
hash node per live order (500k nodes, tens of MB) on demand, so the process keeps faulting in new heap pages
while "trading"; A adds a tree node per level. C's memory is a few large contiguous blocks (the level array
and the doubling order table), so it faults less for a comparable amount of data. I did not find out why A is
above B, or whether the kernel backs C's big table with huge pages. The takeaway is that A and B allocate
during operation (a latency risk), whereas C's allocation is concentrated in 4 predictable table doublings
that pre-sizing would remove.

### 11.5 Production choice

I would deploy **C**, but not only because it is fastest (41.5 ms vs 112.1 ms vs 1,694.8 ms for the same work).

- **Latency and predictability:** C has no allocator calls on the hot path (except the rare table doubling) and
  no tree rebalancing. Its worst cases are bounded and known: the cursor re-scan after the best level empties
  (at most 10,000 array slots, sequential) and a table doubling (a copy of up to 12 MB, removable by
  pre-sizing). A depends on the allocator and on pointer chasing that can miss at any level. B has large
  tails: a re-scan of up to 5,000 levels whenever a cancel empties the best level, and a depth query that
  copies and sorts every level.
- **Memory:** C is fixed and visible: 80 KB of levels plus a 24 MB table sized for about 2 slots per live
  order. A and B pay allocator overhead per node and grow without a cap.
- **Cache behaviour:** the best of the three (11.3).
- **Complexity and maintainability:** this is C's cost. It has the most code (a custom hash table with
  backward-shift deletion), and the array hard-codes the 10,000-tick band, a fixed rule here that would need
  rework if the instrument's range changed. A is the simplest and most general (about 60 lines, sorted for
  free, easy to extend with a queue of orders per level for price-time priority). I would keep A in the
  repository as the reference that `--verify` cross-checks C against.

### 11.6 What would I try next

- Pre-size C's order table to remove the rehash spikes and startup page faults.
- Replace the cursor re-scan with a bitmap of non-empty levels and a count-trailing-zeros search: O(1)
  best-price recovery and fewer data-dependent branches.
- Shrink C's 24-byte table slot (32-bit quantity, packed index/side) so more slots fit per cache line; if
  order IDs are dense, index a plain vector by ID and skip hashing.
- Give A and B a pool allocator for their nodes to see how much of their cost is `malloc`/`free`.
- Make B's depth query a `partial_sort` (O(L)) instead of a full sort, or keep its levels in a sorted vector.
- Re-run on a Linux machine with hardware counters (the lab machines) to fill in the unavailable rows and
  confirm the inferences in 11.2 and 11.3, and time operations in batches so timer overhead does not dominate.

## Appendix: `perf stat -r 5` output (Linux VM)

Command: `perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X`. Full files, and the `perf record -g` / `perf report` output, are in `results/`.

### Implementation A

```
 Performance counter stats for './bench --impl A' (5 runs):

            876.08 msec task-clock                       #    1.157 CPUs utilized            ( +-  3.52% )
   <not supported>      cycles                                                      
   <not supported>      instructions                                                
   <not supported>      branches                                                    
   <not supported>      branch-misses                                               
   <not supported>      cache-misses                                                
   <not supported>      L1-dcache-load-misses                                       
   <not supported>      dTLB-load-misses                                            
              9519      page-faults                      #   12.586 K/sec                    ( +-  2.01% )

            0.7574 +- 0.0308 seconds time elapsed  ( +-  4.06% )
```

### Implementation B

```
 Performance counter stats for './bench --impl B' (5 runs):

          12609.83 msec task-clock                       #    0.984 CPUs utilized            ( +-  0.73% )
   <not supported>      cycles                                                      
   <not supported>      instructions                                                
   <not supported>      branches                                                    
   <not supported>      branch-misses                                               
   <not supported>      cache-misses                                                
   <not supported>      L1-dcache-load-misses                                       
   <not supported>      dTLB-load-misses                                            
              8463      page-faults                      #  660.320 /sec                     ( +-  2.96% )

           12.8194 +- 0.0927 seconds time elapsed  ( +-  0.72% )
```

### Implementation C

```
 Performance counter stats for './bench --impl C' (5 runs):

            379.32 msec task-clock                       #    0.970 CPUs utilized            ( +-  1.02% )
   <not supported>      cycles                                                      
   <not supported>      instructions                                                
   <not supported>      branches                                                    
   <not supported>      branch-misses                                               
   <not supported>      cache-misses                                                
   <not supported>      L1-dcache-load-misses                                       
   <not supported>      dTLB-load-misses                                            
              6904      page-faults                      #   17.679 K/sec                    ( +-  2.34% )

           0.39120 +- 0.00389 seconds time elapsed  ( +-  0.99% )
```
