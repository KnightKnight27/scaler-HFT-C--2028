# Capstone: Low Latency Order Book

Name: Harshita Hirawat  
Roll no: 24BCS10044  
Email: harshita.24bcs10044@sst.scaler.com

An order book for one instrument. Orders rest in the book until they are cancelled (no matching). I wrote it 3 times with 3 different data structures, checked that all 3 give the same results, and then benchmarked and profiled them.

| | Design | Idea |
|--|--|--|
| **A** | priority queue (heap) with lazy deletion | priority queue gives the best price on top, but you can't delete from the middle, so lazy deletion with a delete map |
| **B** | sorted arrays with branchless binary search | bids array high to low, asks array low to high, insert in the middle, branchless binary search |
| **C** | my own design: one array indexed by price | index = price, bids and asks in one array meeting in the middle, linear search near the top of the book |

## Files

| File | What it is |
|------|------------|
| `common.h` | `Side`, `Level`, `BBO`, price band, `printPrice` |
| `pq_book.h` | Implementation A |
| `vector_book.h` | Implementation B |
| `array_book.h` | Implementation C |
| `workload.h` | the random 1M op workload (seed 42), used by both programs |
| `bench.cpp` | benchmark, `--impl A/B/C` |
| `test.cpp` | correctness test |
| `run_all.sh` | builds, tests, benchmarks and runs perf for all 3, saves output in `results/` |
| `results/` | my outputs: `verify.txt`, `bench_X.txt`, `perf_stat_X.txt`, `perf_report_X.txt` |

## How to build

```
g++ -O2 -std=c++17 bench.cpp -o bench
g++ -O2 -std=c++17 test.cpp -o test
```

## How to run the correctness test

```
./test
```

## How to run each benchmark

```
./bench --impl A      # 5 runs, prints every run and the median
./bench --impl B
./bench --impl C
```

For perf I use `--runs 1` so that `perf stat -r 5` does exactly 5 runs:

```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A --runs 1
perf record -g ./bench --impl A --runs 1
perf report
```

Or all of it at once, pinned to one core: `taskset -c 2 ./run_all.sh`

## Interface changes

1. **Prices are integer ticks, not double.** `1.0 + 1.3 + 3.3 == 5.6` is not always true because floats are only approximate, so prices are stored as whole numbers: 99.98 is `9998`. The decimal is added back only when printing. So `price` in `add_order`, `Level` and `BBO` is `int64_t`, and qty is `int64_t` too. I used signed `int64_t` because C does `price - 5000`.
2. **In A, `get_bbo`, `get_bids` and `get_asks` are not `const`**, because the lazy deletion cleans the heap inside get BBO. B and C keep `const`.

There is no `double` anywhere in the code.

## My data structure choices

L = number of price levels on one side (max 5000), d = depth asked for.

There is no matching, so the order of orders at the same price never changes any answer. So every book keeps the **total qty of each price level**, plus each order's qty so it can be cancelled.

### A: priority queue with lazy deletion

* **Data structures:** max heap of bid prices, min heap of ask prices (`std::greater`), `unordered_map` price to total qty for each side, a delete map for each side, `unordered_map` order id to order.
* **Orders:** in the order map, id to `{side, price, qty}`.
* **Price levels:** the heap has only the prices, the qty is in the qty map. A price is pushed when its level goes from 0 to some qty.
* **Best bid/ask:** in get BBO: `while (delete_map[top] > 0) pop`, then the top is the best price and the qty map gives its qty.
* **Cancel:** find the order by id and lower the qty of its price. If the level becomes 0, the price stays in the heap and I only do `delete_map[price]++`.
* **Why it is correct:** copies of a price in the heap = its delete count + 1 if it still has qty. So after the cleanup the top always has qty.

| add_order | cancel_order | get_bbo | get_bids / get_asks |
|--|--|--|--|
| O(1) avg, O(log L) for a new price | O(1) avg | O(1), plus O(m log L) to pop m dead prices | O(L) heap copy + O(d log L) pops |

### B: sorted arrays with branchless binary search

* **Data structures:** `std::vector<Level>` for bids (high to low) and asks (low to high), for example 10, 9, 8 and 11, 12, 13, and an `unordered_map` order id to order.
* **Orders:** in the order map.
* **Price levels:** `{price, qty}` 16 bytes each, all next to each other. Best price is at index 0.
* **Best bid/ask:** `mBids[0]` and `mAsks[0]`.
* **Cancel:** the order map gives the price, binary search gives the level, lower the qty, erase the level if it is 0.
* **Search:** branchless binary search with `s`, `length`, `half`, `condition`, where the 0 or 1 condition is multiplied instead of using if else. The middle is `arr[s + half]`, not `arr[half]`, because once `s` moves the middle moves too (my test fails with `arr[half]`).

| add_order | cancel_order | get_bbo | get_bids / get_asks |
|--|--|--|--|
| O(log L) search, + O(L) shift for a new price | O(log L), + O(L) shift if the level empties | O(1) | O(d) |

### C: one array indexed by price (my own design)

Built from 3 ideas: index = price, linear search because most trading is at the top of the book, and bids and asks in one array so the best bid and best ask are next to each other.

* **Data structures:** one `std::vector<int64_t>` of 10001 entries for both sides, a `std::vector<Slot>` indexed by order id, and 2 ints for the best indexes.
* **Price levels:** `index = price - 5000`. Index 0 to 4999 are bids (50.00 to 99.99), 5000 is the mid (never used), 5001 to 10000 are asks (100.01 to 150.00). The entry is the total qty at that price.
* **Orders:** `mOrders[id] = {qty, index}`. Ids are unique and go up, so the id is the index. 2^20 slots are made once in the constructor. It only grows if an id goes past 2^20, which never happens in this workload (about 700,000 adds).
* **Best bid/ask:** kept in `mBestBid` and `mBestAsk`, updated on add with one compare. If a cancel empties the best level, it walks to the next level with qty.
* **Cancel:** `mOrders[id]`, no hashing and no search.
* **Memory:** all taken once at the start, nothing new while orders come in.
* **Limits:** only works because the price band is small and fixed and the ids are increasing. No range check on price, the assignment says prices are always in the band.

| add_order | cancel_order | get_bbo | get_bids / get_asks |
|--|--|--|--|
| O(1) | O(1), + O(gap) walk if the best level empties | O(1) | O(d + empty levels skipped) |

## Correctness results

`./test` checks every implementation in 2 ways:

1. **Small cases:** empty book BBO, unknown cancel returns false, double cancel returns false, same price orders add up, 10 levels asked when only 3 exist, a level that empties and gets orders again, asks lowest first, empty and refill, the 4 price band edges, and a new price on an empty side, above all, below all, in between and on an existing level.
2. **Full workload:** the same 1M ops as the benchmark go through the book and through a simple reference book (`std::map` of price to qty). After every op it compares the cancel result, the BBO and the top 10 levels on both sides. Every 10,000 ops it compares the whole book. Every cancel is done twice and the second one must be false.

```
Running correctness test...
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

All 3 also end the benchmark with the same final BBO and checksum. To check the test really catches mistakes, I broke each book on purpose (wrong walk in C, `arr[half]` in B, wrong lazy deletion in A) and it failed every time.

## Benchmark

* 1,000,000 ops, `std::mt19937 rng(42)`: 70% add, 20% cancel, 10% get_bbo, and `get_bids(10)` + `get_asks(10)` every 100 ops.
* Side 50/50, qty 1 to 100, price a random tick inside its band. Cancels always pick a live order.
* The workload is made before timing, so the same ops go to all 3.
* Every op is timed with `std::chrono::steady_clock` (no kernel call, no NTP changes). Two clock calls cost about 27 to 29 ns, and that is included in every number.
* 5 runs, median of each value. p99 is printed too. Final BBO and checksum are printed so the compiler can't skip the work.

```
A  MEDIAN  total 340.3ms | add 199 ns (p99 636) | cancel 533 ns (p99 1970) | bbo 103 ns (p99 170)
B  MEDIAN  total 357.9ms | add 253 ns (p99 861) | cancel 637 ns (p99 2528) | bbo 52 ns (p99 86)
C  MEDIAN  total 90.1ms  | add 34 ns (p99 56)   | cancel 125 ns (p99 632)  | bbo 47 ns (p99 63)

Final BBO (same for all 3):
Bid: 99.99 x 1981
Ask: 100.01 x 2573
Checksum: 12504287800
```

**Machine:** My laptop runs Ubuntu inside WSL2, which is a VM, and perf stat could not read the hardware counters there (cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses and dTLB-load-misses all showed `<not supported>`). So I ran the same code with `run_all.sh` on my friend's native Linux machine, and all results in this README and in `results/` are from that machine: Intel i5 1235U, Ubuntu 24.04.5, g++ 13.3.0, perf 6.8.12, pinned to one performance core with `taskset -c 2`. Between runs A and B changed by up to about 20%, C by less than 4%.

## perf stat output

The i5 1235U has 2 types of cores (performance cores and efficiency cores), so perf prints a `cpu_core` row and a `cpu_atom` row for every counter. The `cpu_atom` rows are `<not counted>` because the program was pinned to a performance core, so the `cpu_core` rows are the real ones.

perf stat counts the whole program, so it also includes making the workload and sorting the latencies for p99. That part is the same for all 3, so the differences between the books are smaller here than in the benchmark.

Implementation A:
```
            565.40 msec task-clock                       #    1.000 CPUs utilized               ( +-  3.50% )
     <not counted>      cpu_atom/cycles/                                                        (0.00%)
     1,132,998,251      cpu_core/cycles/                 #    2.004 GHz                         ( +-  2.59% )
     <not counted>      cpu_atom/instructions/                                                  (0.00%)
     1,373,802,043      cpu_core/instructions/           #    1.21  insn per cycle              ( +-  0.08% )
     <not counted>      cpu_atom/branches/                                                      (0.00%)
       233,116,999      cpu_core/branches/               #  412.306 M/sec                       ( +-  0.10% )
     <not counted>      cpu_atom/branch-misses/                                                 (0.00%)
         6,096,038      cpu_core/branch-misses/          #    2.62% of all branches             ( +-  1.04% )
     <not counted>      cpu_atom/cache-misses/                                                  (0.00%)
         8,298,507      cpu_core/cache-misses/                                                  ( +-  4.96% )
   <not supported>      cpu_atom/L1-dcache-load-misses/                                       
        27,305,471      cpu_core/L1-dcache-load-misses/                                         ( +-  0.09% )
     <not counted>      cpu_atom/dTLB-load-misses/                                              (0.00%)
           809,264      cpu_core/dTLB-load-misses/                                              ( +-  0.86% )
            19,334      page-faults                      #   34.195 K/sec                       ( +-  0.00% )

            0.5655 +- 0.0201 seconds time elapsed  ( +-  3.56% )
```

Implementation B:
```
            567.58 msec task-clock                       #    0.999 CPUs utilized               ( +-  5.19% )
     <not counted>      cpu_atom/cycles/                                                        (0.00%)
     1,134,761,974      cpu_core/cycles/                 #    1.999 GHz                         ( +-  4.69% )
     <not counted>      cpu_atom/instructions/                                                  (0.00%)
     1,329,758,620      cpu_core/instructions/           #    1.17  insn per cycle              ( +-  0.03% )
     <not counted>      cpu_atom/branches/                                                      (0.00%)
       219,288,573      cpu_core/branches/               #  386.355 M/sec                       ( +-  0.04% )
     <not counted>      cpu_atom/branch-misses/                                                 (0.00%)
         5,896,876      cpu_core/branch-misses/          #    2.69% of all branches             ( +-  1.22% )
     <not counted>      cpu_atom/cache-misses/                                                  (0.00%)
         9,242,415      cpu_core/cache-misses/                                                  ( +-  5.93% )
   <not supported>      cpu_atom/L1-dcache-load-misses/                                       
        12,086,099      cpu_core/L1-dcache-load-misses/                                         ( +-  2.34% )
     <not counted>      cpu_atom/dTLB-load-misses/                                              (0.00%)
           797,126      cpu_core/dTLB-load-misses/                                              ( +-  5.72% )
            19,203      page-faults                      #   33.833 K/sec                       ( +-  0.00% )

            0.5683 +- 0.0294 seconds time elapsed  ( +-  5.17% )
```

Implementation C:
```
            221.34 msec task-clock                       #    1.007 CPUs utilized               ( +-  2.71% )
     <not counted>      cpu_atom/cycles/                                                        (0.00%)
       482,521,829      cpu_core/cycles/                 #    2.180 GHz                         ( +-  2.66% )
     <not counted>      cpu_atom/instructions/                                                  (0.00%)
       833,216,419      cpu_core/instructions/           #    1.73  insn per cycle              ( +-  0.03% )
     <not counted>      cpu_atom/branches/                                                      (0.00%)
       135,853,299      cpu_core/branches/               #  613.788 M/sec                       ( +-  0.04% )
     <not counted>      cpu_atom/branch-misses/                                                 (0.00%)
         4,023,083      cpu_core/branch-misses/          #    2.96% of all branches             ( +-  1.14% )
     <not counted>      cpu_atom/cache-misses/                                                  (0.00%)
         2,008,080      cpu_core/cache-misses/                                                  ( +-  3.30% )
   <not supported>      cpu_atom/L1-dcache-load-misses/                                       
         1,903,019      cpu_core/L1-dcache-load-misses/                                         ( +-  0.43% )
     <not counted>      cpu_atom/dTLB-load-misses/                                              (0.00%)
            52,577      cpu_core/dTLB-load-misses/                                              ( +-  2.90% )
            16,422      page-faults                      #   74.195 K/sec                       ( +-  0.00% )

           0.21989 +- 0.00590 seconds time elapsed  ( +-  2.68% )
```

The page faults are for the whole program. On my laptop the same program with an empty book gave 12,334, and the totals there were almost the same as here (19,337, 19,211, 16,430). So the book itself causes about **7,000 (A), 6,870 (B) and 4,090 (C)**. C's part is its 16 MB order array made in the constructor (16 MB / 4 KB = 4,096 pages).

**perf record** (full output in `results/perf_report_X.txt`):
* A: `cancel_order` 17.3%, the price maps (qty and delete maps) 16.6%, sorting the latencies for p99 9.3%, hash map rehash 5.9%, `memmove` (the heap copy in get_bids) 5.8%, the clock 4.2%.
* B: `runOnce` 17.8% (add and get_bbo are inlined into it), `cancel_order` 16.2%, sorting for p99 10.7%, the clock 7.5%, hash map rehash 6.3%, `malloc_consolidate` 3.4%.
* C: `runOnce` 19.1% (the whole book is inlined into it), sorting for p99 17.1%, the clock 15.1%, making the workload 11.1%. So in C most of the time is the benchmark itself, not the book.

## Results table

| Metric | Implementation A | Implementation B | Implementation C |
|--------|------------------|------------------|------------------|
| Total time (ms) | 340.3 | 357.9 | 90.1 |
| ns / add | 199 | 253 | 34 |
| ns / cancel | 533 | 637 | 125 |
| ns / BBO | 103 | 52 | 47 |
| IPC | 1.21 | 1.17 | 1.73 |
| Branch misses (%) | 2.62 (6.10M) | 2.69 (5.90M) | 2.96 (4.02M) |
| L1-dcache-load-misses | 27,305,471 | 12,086,099 | 1,903,019 |
| Cache misses | 8,298,507 | 9,242,415 | 2,008,080 |
| Page faults | 19,334 | 19,203 | 16,422 |

Times are the benchmark medians. The rest is from perf stat (whole program, average of 5 runs).

## Performance analysis

### 11.1 Which implementation is fastest?

**C is fastest for every operation, about 4 times faster than A and B in total (90 ms vs 340 to 358 ms).**

* **add:** C 34 ns vs 199 (A) and 253 (B). In C an add is one array write, one slot write and one compare. No search, no hashing, no new memory.
* **cancel:** C 125 ns vs 533 (A) and 637 (B). C goes straight to `mOrders[id]`, A and B need a hash lookup first. C's cancel is slower than its add because the cancelled id is random, so it is a random read in a 16 MB array that doesn't fit in cache (about one RAM trip, around 100 ns).
* **BBO:** C 47 ns and B 52 ns, both just read the best level directly, and about 27 ns of that is the clock. A is about 2 times slower (103 ns) because for each side it checks the delete map and then gets the qty from the qty map. So a heap needs at least two different memory places for every BBO.
* **A vs B:** they are about the same. A had the lower median (340 vs 358 ms), but B's runs went from 309 to 370 ms, and in perf stat both take 565 to 568 ms. With this workload all 5000 prices on each side fill up very early, so new prices and empty levels are rare. That means B's shifting and A's heap push and pop almost never happen, and both books spend most of their time in the order hash map. B's add is a bit slower because every add still does a 13 step binary search over 80 KB.

### 11.2 Branch prediction

| | A | B | C |
|--|--|--|--|
| branches | 233M | 219M | 136M |
| branch misses | 6.10M (2.62%) | 5.90M (2.69%) | 4.02M (2.96%) |

* **C has the fewest misses (4.02M), A the most (6.10M), B is close to A.** C's percentage looks highest only because C runs a lot fewer branches in total.
* A big part of the misses is the same in all 3: the random `if (op.type == ...)` in the benchmark loop, making the workload, and sorting random latencies for p99. So I think about 4M is the benchmark itself, and A and B add about 2M on top of it.
* **A:** heap push and pop compare with parent and children (about 50/50 with random prices), get_bids pops a copy of the heap every 100 ops, and A uses 5 hash maps.
* **B:** 14M fewer branches than A because the binary search is branchless (I checked with `objdump` that the compare becomes a `cmov`). The misses are still close to A's because the hash map is the same.
* **C:** `if (i > mBestBid)` is almost always false and the walk after a cancel almost always stops at once, so these branches are easy to predict.

### 11.3 Cache behavior

| | A | B | C |
|--|--|--|--|
| L1-dcache-load-misses | 27.3M | 12.1M | 1.9M |
| cache-misses | 8.3M | 9.2M | 2.0M |
| dTLB-load-misses | 809K | 797K | 53K |
| IPC | 1.21 | 1.17 | 1.73 |

* **A has the most L1 misses (14 times C).** Every BBO goes heap, delete map, qty map, and every get_bids copies the whole heap and pops it (`memmove` is 5.8% in perf record). Hash map nodes are small separate allocations, so lookups jump around memory and follow pointers.
* **B** has half of A's L1 misses. The levels are contiguous, but each side is 80 KB, which is more than the 48 KB L1 of this CPU, so the 13 jumps of the search still miss L1 (they mostly hit L2).
* **A and B have about the same last level cache misses and dTLB misses.** Both come mostly from the order hash map: up to 500,000 live orders, so the nodes and buckets are bigger than the 12 MB L3, and every lookup is a random read in it.
* **C** is all arrays and nothing is allocated per order. Best bid and best ask are 16 bytes apart, usually in one cache line. The main miss is the random `mOrders[id]` read on cancel. It has 4 to 5 times fewer cache misses and 15 times fewer dTLB misses.
* Because C waits less on memory, its IPC is the highest (1.73 vs 1.21 and 1.17), and it also runs fewer instructions (833M vs 1374M and 1330M).

So C wins because it uses every cache line it loads, not because of big O.

### 11.4 Page faults

**A causes the most (19,334), B almost the same (19,203)**, about 27 MB each from the book, because both make one hash map node per order. This memory grows while orders come in, so the faults happen during trading. **C has the fewest (16,422)** and its part happens in the constructor before any order comes in. The downside is C always uses 16 MB, even when empty.

### 11.5 Production choice

I would choose **C**:
* **Latency:** lowest for add, cancel and BBO.
* **Predictability:** no new memory while orders come in, so no slow allocation or page fault in the middle. Its p99 add is lower than the average add of A and B.
* **Memory:** fixed and known at the start (80 KB levels + 16 MB order slots).
* **Cache behavior:** all arrays, BBO in one place.
* **Complexity and maintainability:** shortest code, just arrays and indexes.

But it only works for a small fixed price band and increasing ids. For a much wider price range or random ids I would need to change it (move the array around the current price, or use a hash map for ids). A and B are more general but about 4 times slower.

### 11.6 What would I try next?

* Run perf only on the timed loop, so making the workload and sorting for p99 are not in the counters.
* A more realistic workload with most orders near the top of the book.
* `reserve()` the hash maps in A and B, and keep the qty next to the price in A's heap.
* Make C's order slot 8 bytes instead of 16 to halve the order array.
* Compare B's branchless search with a normal binary search and a linear search from index 0.
