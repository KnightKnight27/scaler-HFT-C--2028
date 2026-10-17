# Performance Report

Student: Mohammed Fazil Momin  
Roll Number: `24BCS10278`  

Workload: 1,000,000 operations, seed 42, 70% add / 20% cancel / 10% BBO,
depth queries every 100 operations. 5 runs each, median reported.
Built with `g++ -std=c++20 -O2`. Run on Ubuntu 26.04 (WSL2), Intel i7-13650HX.

## Results table

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 6901 | 1041 | 236 |
| ns / add | 256 | 1028 | 172 |
| ns / cancel | 523 | 759 | 390 |
| ns / BBO | 73 | 410 | 30 |
| Page faults | 33,297 | 33,799 | 31,943 |

IPC, branch misses and cache misses are not available: WSL2 does not expose the
CPU hardware counters, so perf reports them as `<not supported>`. Only
`task-clock` and `page-faults` could be measured.

All three give the same final result:

```
Bid: 99.99 x 2542
Ask: 100.01 x 2355
Checksum: 1491614064682522636
```

## perf stat output

### A

```
 Performance counter stats for './bench --impl A --runs 1' (5 runs):

        7003130953      task-clock:u                     #    0.999 CPUs utilized               ( +-  0.16% )
   <not supported>      cycles:u
   <not supported>      instructions:u
   <not supported>      branches:u
   <not supported>      branch-misses:u
   <not supported>      cache-misses:u
   <not supported>      L1-dcache-load-misses:u
   <not supported>      dTLB-load-misses:u
             33297      page-faults:u                    #    4.755 K/sec                       ( +-  0.00% )

            7.0075 +- 0.0113 seconds time elapsed  ( +-  0.16% )
```

### B

```
 Performance counter stats for './bench --impl B --runs 1' (5 runs):

        1343223032      task-clock:u                     #    0.997 CPUs utilized               ( +-  1.84% )
   <not supported>      cycles:u
   <not supported>      instructions:u
   <not supported>      branches:u
   <not supported>      branch-misses:u
   <not supported>      cache-misses:u
   <not supported>      L1-dcache-load-misses:u
   <not supported>      dTLB-load-misses:u
             33799      page-faults:u                    #   25.163 K/sec                       ( +-  0.00% )

            1.3477 +- 0.0248 seconds time elapsed  ( +-  1.84% )
```

### C

```
 Performance counter stats for './bench --impl C --runs 1' (5 runs):

         457907356      task-clock:u                     #    0.992 CPUs utilized               ( +-  1.55% )
   <not supported>      cycles:u
   <not supported>      instructions:u
   <not supported>      branches:u
   <not supported>      branch-misses:u
   <not supported>      cache-misses:u
   <not supported>      L1-dcache-load-misses:u
   <not supported>      dTLB-load-misses:u
             31943      page-faults:u                    #   69.759 K/sec                       ( +-  0.00% )

           0.46143 +- 0.00719 seconds time elapsed  ( +-  1.56% )
```

## perf record

Full output in `results/perf_report_*.txt`. Symbols show as addresses because
the WSL perf build has no libelf.

- **A:** almost all samples are in the program itself, with two addresses taking
  over 20%. That is the depth query, which sorts all live levels on every call.
- **B:** two addresses take 18% each. Those are the two tree comparators. About
  14% is in libstdc++ and libc, which is tree rebalancing and malloc/free.
- **C:** libc (malloc/free) is the biggest item at 18%. The order book code
  itself is very cheap.

## 11.1 Which implementation is fastest?

C is fastest for all three operations.

- **add:** C finds the price level by array index. A pushes onto a heap. B
  inserts into a tree, which is the slowest.
- **cancel:** C removes one list node. A updates a hash map. B erases from a
  tree and rebalances.
- **BBO:** C reads two stored integers. A has to pop stale heap entries first.
  B walks every order at the best price to add up the quantity.

A has the worst total time because its depth query sorts every live level on
each call.

## 11.2 Branch prediction

Hardware counters were not available. From the code, B should have the most
branch misses: every tree insert and erase is a chain of left/right decisions
on random prices. A's sort is next. C's add path has almost no branches.

## 11.3 Cache behaviour

Hardware counters were not available. From the layout, B should miss the most:
every order is a separate heap-allocated tree node, so it follows pointers into
scattered memory and allocates many small objects. A's heap is contiguous but
grows with stale entries. C's price levels are one contiguous array, so finding
a level is a single load.

## 11.4 Page faults

B 33,799, A 33,297, C 31,943. They are close because most faults come from the
1,000,000-entry workload array in the benchmark itself. B is highest because it
allocates one node per order. C is lowest because its level arrays are
allocated once at start-up.

## 11.5 Production choice

**C.** It is the fastest, and more importantly its cost per operation does not
grow with the number of orders, so latency is predictable. Memory is fixed at
start-up. The code is simple because the price is the array index.

Trade-off: the arrays are sized to the spec's price band, and a cancel that
empties the best level has to scan to the next one.

B is the one to avoid: tree depth and BBO cost both grow with the book. A is
fine for add and cancel but its depth query is too slow.
