# Performance Analysis Report

Krritin Keshan (24BCS10122), Abdullah Danish (24BCS10054)

## Setup

We ran everything on one mac: Apple M3 Pro, 12 cores, 18 GB RAM, macOS 26.3.

- benchmarks: built with Apple clang 17 (`-std=c++17 -O2`) and run directly on the mac
- perf and cachegrind: inside a Debian docker container on the same mac, built with g++ 12.2 (`-std=c++17 -O2 -g -fno-omit-frame-pointer`)

Docker on Apple Silicon doesn't give any hardware counters, so in perf stat only task-clock and page-faults worked. The mac itself does have them though, so we read them with Instruments (`xctrace`, CPU Counters template with the events FIXED_CYCLES, FIXED_INSTRUCTIONS, INST_BRANCH, BRANCH_MISPRED_NONSPEC, L1D_CACHE_MISS_LD_NONSPEC and L1D_TLB_MISS). IPC is from `/usr/bin/time -l`, which also prints instructions and cycles but without a profiler attached. The M3 has no event for last level cache misses, so only that one row comes from cachegrind (a simulation).

The counters are measured with `./bench --impl X --perf` (one run, no per op timers). That run also generates the 1M ops, so we also measured `--impl none --perf` (only generates the ops) and subtracted it from A, B and C.

## Correctness

Output of `./bench --verify`:

```
Running correctness test...

edge cases: A ok, B ok, C ok
workload 1: 1000000 ops, seed 42, 70% add / 20% cancel, full price range (same as the benchmark)
workload 2: 200000 ops, seed 42, 40% add / 50% cancel, prices within 20 ticks of the mid
            (book stays small, best levels keep getting empty, sometimes a whole side is empty)
after every op B and C are compared with A: cancel result, bbo, top 10 bids and asks
(full book every 10000 ops)
mismatches: B 0, C 0

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

The three benchmark runs also ended with the same final BBO (Bid 99.99 x 2522, Ask 100.01 x 2193) and the same checksum, 4459131921.

We also wanted to make sure the test can actually catch a bug, so we broke C once (removed the loop that moves the best bid down) and B once (didn't reset `inHeap`). It caught both, with 178,844 and 1,136,202 mismatches.

## Results Table

| Metric | Implementation A | Implementation B | Implementation C |
| ----- | ----- | ----- | ----- |
| Total time (ms) | 135.09 | 117.45 | 12.65 |
| ns / add | 111.0 | 60.9 | 16.4 |
| ns / cancel | 249.7 | 282.9 | 23.7 |
| ns / BBO | 18.1 | 15.1 | 15.9 |
| IPC | 1.70 | 1.98 | 3.58 |
| Branch misses (%) | 2.4% (5.35M) | 1.1% (2.50M) | 1.9% (0.64M) |
| L1-dcache-load-misses | 14.86M | 21.97M | 0.76M |
| Cache misses (last level, simulated) | 2.47M | 0.93M | 0.27M |
| Page faults | 11,776 | 7,451 | 2,532 |
| dTLB misses | 10.46M | 7.99M | 0.55M |

Where the numbers come from:
- total time and ns per op: `./bench --impl X`, median of 5 runs (`results/bench.txt`). Every ns number includes one clock read, which costs about 14 ns on this mac, so a BBO is really only 1 to 4 ns for all three.
- IPC: `/usr/bin/time -l`, median of 5 runs, with the baseline subtracted from both instructions and cycles (`results/mac_time.txt`). Without subtracting it's 1.78, 2.04 and 3.03.
- branch misses, L1 load misses and dTLB misses: real counters from Instruments, median of 5 runs, baseline subtracted (`results/mac_counters.txt`). The baseline was 0.44M branch misses, 0.84M L1 load misses and 0.44M dTLB misses. The dTLB event on the M3 counts loads and stores together.
- cache misses: cachegrind "LL misses" with the baseline subtracted (`results/cachegrind.txt`). This row is simulated.
- page faults: perf stat with the baseline subtracted (`results/perf_stat.txt`)

## perf stat

```
 Performance counter stats for '/tmp/bench --impl A --perf' (5 runs):

            250.60 msec task-clock                       #    1.020 CPUs utilized            ( +-  1.03% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             14033      page-faults                      #   57.352 K/sec                    ( +-  0.92% )

           0.24580 +- 0.00257 seconds time elapsed  ( +-  1.04% )
```

```
 Performance counter stats for '/tmp/bench --impl B --perf' (5 runs):

            136.37 msec task-clock                       #    0.974 CPUs utilized            ( +-  1.32% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              9708      page-faults                      #   69.780 K/sec                    ( +-  1.05% )

           0.13998 +- 0.00180 seconds time elapsed  ( +-  1.29% )
```

```
 Performance counter stats for '/tmp/bench --impl C --perf' (5 runs):

             25.22 msec task-clock                       #    0.980 CPUs utilized            ( +-  0.97% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              4789      page-faults                      #  187.901 K/sec                    ( +-  1.11% )

          0.025731 +- 0.000253 seconds time elapsed  ( +-  0.98% )
```

The baseline run (`--impl none`) is in `results/perf_stat.txt` too: 13.71 ms and 2,257 page faults.

## perf record

Docker only gave around 1000 samples per second, so we ran each implementation 40 times under `perf record -g`. Top functions from `perf report --no-children` (the full call graphs are in `results/perf_report.txt`):

A: add_order 27.4%, _int_free 21.0%, cancel_order 19.6%, unordered_map rehash 5.5%, std::map _M_erase 5.1%. Most of the free and _M_erase time is at the end of the run, when the book gets destroyed and every node is freed one by one.

B: cancel_order 35.7% (the std::map search by id is inlined into it), add_order 11.0%, tree rebalancing 5.9% + 3.3%, memcpy 5.1% which is the heap copies in get_bids / get_asks, and around 10% in free and the map destructor.

C: runOp 30.2% (all of C's code got inlined into it) and 43.5% just generating the random ops (makeWorkload + mt19937). So for C, making the workload costs more than running it.

## 1. Which implementation is fastest?

C, for both add and cancel, and overall it's about 10x faster (12.65 ms vs 117.45 ms for B and 135.09 ms for A).

add - C takes 16.4 ns, B 60.9 ns, A 111.0 ns. In C an add is basically a few arithmetic ops and two writes, one into the price array and one into the orders vector. No searching and no allocation. A has to find the price in a red-black tree (5000 levels, around 13 levels deep) and then allocate a list node and a hash map node. B allocates one tree node for the order and does one hash lookup for the price. Order ids keep increasing, so B always inserts at the right end of its tree, which stays in cache, and that helps too.

cancel - C 23.7 ns, A 249.7 ns, B 282.9 ns. Cancel picks a random live order out of up to 500k, so the order's data is almost never in cache. C goes straight to orders[id], one memory access, and then one slot of the price array. A needs a hash lookup, then map.find for the price, then unlinking the list node, and every step has to wait for the previous one, so the cache misses add up. B is the slowest because looking up a random id in a std::map with 500k entries is about 19 levels of pointer chasing.

BBO - all three are almost the same as just reading the clock (about 14 ns). If you take that out, B is around 1 ns, C around 2 ns and A around 4 ns. A is a bit slower because on mac `rbegin()` of a std::map walks down the right side of the tree every time.

The numbers move around a bit between runs (C's cancel was anywhere between 17 and 35 ns across our runs) but the order of A, B and C was always the same.

## 2. Branch Prediction

A has the most branch misses, 5.35M (2.4% of its branches). B has 2.50M (1.1%) and C 0.64M (1.9%). C's percentage is higher than B's only because C runs very few branches (33M vs 220M for B). It actually has about 4 times fewer misses than B and 8 times fewer than A.

In A every add (700k of them) goes down the price tree with a random price, around 13 levels, and at every node "go left or go right" depends on that random price, so the CPU can't really guess it. Cancels do the same thing in map.find. B only searches its tree on cancel (200k times, around 19 levels, again with random ids). Its adds always go to the right end of the order tree because the ids keep increasing, and the predictor gets that right. C has almost no branches inside the book that depend on random data. Most of its ~0.6 misses per op are probably from our benchmark loop itself, since the op type and the side are random.

cachegrind (simulated) gave more misses for all three (8.57M, 6.88M, 1.08M) because its branch predictor is much simpler than the M3's, but the order A > B > C was the same.

## 3. Cache Behavior

B has the most L1 load misses (21.97M), then A (14.86M), and C has only 0.76M, about 20 to 30 times less. For the last level cache (simulated) A is the worst with 2.47M, then B 0.93M and C 0.27M. The dTLB misses look the same: A 10.46M, B 7.99M, C 0.55M.

A follows pointers everywhere: tree nodes for prices, a list node per order and a hash node per order. These are lots of small objects allocated one by one with new, so they end up all over the heap, and one cancel touches 3 or 4 random places. A also uses the most memory per order (about 46 MB in total), so less of it fits in the 16 MB L2 and it is spread over more pages, which is probably why it has the most last level and dTLB misses.

B is also node based. Every cancel searches a tree of up to 500k orders (the lower levels are never in L1), every add and cancel looks up the price in a hash map, and get_bids / get_asks copy the whole 20 KB heap 20,000 times. That's why it ends up with even more L1 misses than A. cachegrind's simple cache model had A and B about equal (9.31M vs 9.66M), but on the real chip B is clearly worse.

C only uses contiguous memory. The price array is 80 KB so it fits in the 128 KB L1, and the best prices sit next to each other. Orders are one vector of 16 byte entries (about 700k ids = 11 MB, fits in the 16 MB L2). Cancel is one access, no pointers to follow.

## 4. Page Faults

A has the most page faults, 11,776, then B with 7,451 and C with 2,532 (after removing the 2,257 that come from generating the ops). With 4 KB pages that's about 46 MB, 29 MB and 10 MB of new memory. The mac shows the same order, peak memory without the workload was 45.6 MB for A, 31.9 MB for B and 19.1 MB for C.

It basically shows how much each one allocates. A allocates two small objects for every order (a list node and a hash node) and its hash table keeps growing (the rehash is 5.5% of A's profile). B allocates one tree node per order. C has the fixed 80 KB array and one vector of 16 byte orders that only doubles a few times. More small allocations means more memory, more new pages and more page faults. It also costs time at the end, about a quarter of A's profile is free() and the map destructor.

## 5. Production Choice

We would deploy C.

Latency - it's the fastest for add, cancel and BBO, and about 10x faster overall.

Predictability - there is no allocation per order (only when the orders vector grows, and that can be avoided with reserve), no tree rebalancing and no rehashing. The only slow case is when the best level becomes empty and we have to walk to the next price. In the benchmark that almost never happens since every price has around 50 orders by the end, but on a thin book it could be a walk of up to 5000 slots.

Memory - 80 KB for all price levels and 16 bytes per order, the least of the three. The catch is that the orders vector grows with the biggest order id, not with how many orders are live.

Cache - everything is contiguous, and it has by far the fewest L1, last level and TLB misses.

Complexity and maintainability - it's the simplest code of the three, mostly just array indexing. No iterators that need to stay valid.

C isn't perfect though. It only works because the price range is small and fixed (10,000 ticks) and the order ids are small increasing numbers. With real exchange ids (random 64 bit numbers) we'd need a hash map from id to slot. It also doesn't remember the time order of orders at a price, only the total qty. That's fine here because there's no matching, but if we needed matching, A is the one that already keeps a FIFO list per price (it's just 10x slower).

## 6. What would we try next?

- a bitmap of non-empty prices for C, so when the best level empties we find the next one by checking a few 64 bit words instead of walking up to 5000 slots
- an open addressing hash map from order id to slot in C, so any 64 bit id works and memory depends on live orders
- reserve the orders vector at the start so it never grows during trading
- a FIFO list of orders per price in C (using indexes into the orders vector) for time priority
- a memory pool for the nodes in A and B instead of one new per order
- in B, read the top 10 levels without copying the whole heap
- smaller structs (int qty instead of long) so more orders fit in a cache line
