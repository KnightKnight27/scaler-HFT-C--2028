# Order Book Results

This report compares the three versions in [README.md](README.md). A uses maps, B uses sorted vectors, and C uses a price array, a bitmap and a custom hash table. The figures below are from the saved runs in `results/`, rather than a new measurement on every build.

## 1. Setup

| | |
|---|---|
| Machine | MacBook Pro, Intel Core i7-9750H (Coffee Lake, 6 cores / 12 threads, 2.6 GHz, turbo up to 4.5 GHz), 16 GB RAM |
| Caches | 32 KB L1d and 256 KB L2 per core, 12 MB shared L3, 64-byte lines |
| macOS build | macOS 15.7.7, Apple clang 17.0.0 (libc++) |
| Linux build | Ubuntu 24.04 in Docker Desktop on the same Mac (kernel 7.0.12-linuxkit), g++ 13.3 (libstdc++, glibc malloc), perf 6.8.12, valgrind 3.22 |
| Flags | `-std=c++17 -O2 -g -fno-omit-frame-pointer` |
| Workload | 1,000,000 operations, seed 42: 700,471 add, 199,305 cancel, 100,224 BBO, plus `get_bids(10)` and `get_asks(10)` every 100 operations. At most 501,168 orders rest at the same time. |

The benchmark ran each version five times, alternating A, B and C, and reported the median. The laptop was also running other applications. Some runs were much slower than others, so the absolute timings should be treated as approximate. The relative order was consistent in the saved macOS and Linux runs.

## 2. Correctness

`./build/bench --verify` checks 50 edge cases on each version, compares C's hash table with `std::unordered_map`, and runs all three books alongside a simple reference book. The random tests check the BBO and top 10 levels after every operation, and all levels every 10,000 operations. A separate workload uses frequent cancels to exercise empty levels and order-table cleanup.

The saved macOS and Linux outputs end with:

```
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

See `results/macos/verify.txt` and `results/linux-docker/verify.txt`. All three benchmark runs also have checksum `0xeb883e677224c78a`. These checks cover the tested inputs; they do not prove correctness for every possible input.

## 3. Measurement limits

The Linux runs used Docker Desktop on the Mac. Its VM did not expose hardware performance counters, so `perf stat` reported task time and page faults but marked CPU cycles, instructions and cache events as unsupported.

The IPC figures instead come from macOS process counters. Branch and cache figures come from cachegrind simulations with a workload-only baseline subtracted. They are useful for comparing the versions under the same simulation, but they are not measurements of actual CPU cache misses. No measured dTLB-miss result is available.

The total workload time and individual-call times are measured in separate passes. The second pass subtracts the measured clock overhead, which varied by about 10 ns between invocations. This makes the shortest calls uncertain. BBO is also timed in a loop of 1,000,000 calls because one call is shorter than a clock read.

The benchmark supplies enough capacity to avoid container growth. Its price distribution is uniform within fixed bands and its ids increase. Results with different input patterns or an undersized table may differ.

## 4. Results

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 280.8 | 137.4 | **41.3** |
| ns / add | 241.2 | 91.1 | **30.9** |
| ns / cancel | 461.5 | 299.2 | **97.8** |
| ns / BBO | 3.33 | **3.24** | 3.72 |
| IPC | 0.70 | 0.66 | **0.98** |
| Branch misses (%) | 9.7% (8.8 per op) | 7.9% (9.5 per op) | **6.3% (1.0 per op)** |
| L1-dcache-load-misses | 11.2 M | 10.8 M | **1.3 M** |
| Cache misses | 1.76 M | **0.22 M** | 0.51 M |
| Page faults | 8,781 (+6,402) | **2,248 (about 0)** | 2,491 (+112) |

Sources for the rows:

- Total time, add, cancel and BBO: `results/macos/bench_all.txt`. BBO uses the repeated-call loop. Add and cancel use the individual-call pass after clock-overhead subtraction.
- IPC: macOS process hardware counters during the total-time pass.
- Branch and cache misses: cachegrind output in `results/linux-docker/`, minus the workload-only baseline. These include construction, destruction and the benchmark loop.
- Page faults: Linux `perf stat`, for the whole process. Parentheses show the difference from the workload-only baseline of 2,379 faults.

Additional saved results:

| | A | B | C |
|---|---|---|---|
| Instructions per operation (cachegrind) | 487 | 365 | 129 |
| Branches per operation (cachegrind) | 91 | 120 | 15 |
| p99 / p99.9 add (ns) | 718 / 2,712 | 309 / 1,005 | 179 / 425 |
| p99 / p99.9 cancel (ns) | 1,196 / 2,327 | 1,125 / 3,423 | 396 / 513 |
| Depth query, `get_bids(10)` + `get_asks(10)` (ns) | 449 | 300 | 315 |
| Page faults during the measured pass (macOS), first run / median of 5 | 6,242 / 4,696 | 2,758 / 0 | 0 / 0 |
| Memory used by the book (macOS peak RSS minus the workload-only run) | 25.8 MiB | 10.9 MiB | 16.1 MiB |
| Linux build: total time (ms) | 242.8 | 147.7 | 41.5 |
| Linux build: ns / add, ns / cancel | 234 / 438 | 94 / 297 | 42 / 104 |

The Linux timings have the same ranking despite using a different compiler, standard library and allocator. The percentile figures are also individual-call measurements, so their accuracy has the clock-overhead limitation described above.

## 5. perf stat output

Linux (Docker VM): `perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X --perf`. Each run generates the workload (about 36 ms, see the baseline) and runs it once. Full files: `results/linux-docker/perf_stat_*.txt`.

Implementation A:

```
 Performance counter stats for 'build-linux/bench --impl A --perf' (5 runs):

            401.94 msec task-clock                       #    0.992 CPUs utilized               ( +-  3.63% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              8781      page-faults                      #   21.847 K/sec                       ( +-  1.17% )

            0.4053 +- 0.0146 seconds time elapsed  ( +-  3.59% )
```

Implementation B:

```
 Performance counter stats for 'build-linux/bench --impl B --perf' (5 runs):

            181.05 msec task-clock                       #    0.990 CPUs utilized               ( +-  4.08% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              2248      page-faults                      #   12.417 K/sec                       ( +-  6.85% )

           0.18293 +- 0.00754 seconds time elapsed  ( +-  4.12% )
```

Implementation C:

```
 Performance counter stats for 'build-linux/bench --impl C --perf' (5 runs):

             90.42 msec task-clock                       #    0.978 CPUs utilized               ( +- 10.01% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              2491      page-faults                      #   27.549 K/sec                       ( +-  4.10% )

           0.09247 +- 0.00909 seconds time elapsed  ( +-  9.83% )
```

Baseline, `--impl none` (workload generation only):

```
             35.65 msec task-clock                       #    0.946 CPUs utilized               ( +-  1.11% )
              2379      page-faults                      #   66.738 K/sec                       ( +-  0.01% )
```

Unavailable in this environment: `cycles`, `instructions`, `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and `dTLB-load-misses` (no PMU in the VM). Section 3 explains what replaced them.

## 6. perf record

The saved profiles used `perf record -F 10000 -g` followed by `perf report --no-children`. With hardware counters unavailable, sampling used a software timer. The following percentages are shares of samples for the whole process, including workload generation:

| A | | B | | C | |
|---|---|---|---|---|---|
| 38.9% | `add_order` | 31.7% | `find_order` (binary search over orders) | 32.2% | `add_order` |
| 24.3% | `cancel_order` | 33.8% | `find_level`, both sides (binary search over levels) | 21.8% | `cancel_order` |
| 16.5% | glibc `malloc` / `free` internals | 8.7% | `add_order` | 29.1% | generating the workload |
| 3.8% | the destructor, freeing every node | 2.2% | `memmove` (vector insert and erase) | 3.8% | kernel: page faults, zeroing new pages |
| 6.4% | generating the workload | 13.7% | generating the workload | 1.0% | `get_asks` |

The map and unordered-map code is mostly inlined into A's add and cancel methods. B spends much of its time in binary search. For C, workload generation is a larger fraction of the total because the book itself takes less time. See `results/linux-docker/perf_report_*.txt` for the complete profiles.

## 7. Discussion

### Which version was fastest?

C took 41.3 ms for the workload, compared with 137.4 ms for B and 280.8 ms for A. That is about 3.3 times faster than B and 6.8 times faster than A in this run. C also had the lowest measured add and cancel times. BBO times were close for all three, and B was slightly quicker for depth queries.

The main difference is the work needed to locate a price level. A follows tree nodes, B uses binary search, and C calculates an array index. C still has to search its hash table to find an order by id. It can also resize if the capacity hint is too small.

### Branches and cache misses

In cachegrind, branch misses were about 8.8 per operation for A, 9.5 for B and 1.0 for C. A tree walk or binary search repeatedly compares a random key and chooses a direction. C avoids those price searches, which helps explain the lower count.

A also had the most simulated L1 and last-level cache misses. Its orders are separate heap nodes, so a lookup follows pointers. B stores entries next to each other, but binary search still jumps between distant positions. C directly accesses the quantity array and usually probes only a small part of its order table.

B had fewer last-level cache misses than C. C's table uses more space, and some of its misses come from zero-filling that storage in the constructor. The results suggest that compact storage helps, but compactness alone does not remove search costs.

### Allocation and page faults

A allocates nodes while orders arrive. It had 8,781 Linux page faults, about 6,400 above the workload-only baseline, and the profile also shows time in allocation and deallocation.

B reserves vector capacity and C allocates its table before the timed pass. C also zero-fills its storage then, so its initialization cost is outside the total-time measurement. B's reserved pages are first written as orders are added; this is consistent with its higher first-run macOS fault count. The whole-process Linux figures include initialization as well.

### Choice and limitations

For this workload and fixed price range, C is the fastest choice. It also has the lowest saved p99.9 add and cancel figures. A is simpler to read and change, so it remains useful as a comparison.

C has more implementation details to get right: hash collisions, erase behavior, bitmap boundaries and capacity sizing. The array assumes a small fixed price range. Hash probing and resizing can still cause slow operations. These benchmarks are not enough to call it production ready.

The project stores resting quantities and supports cancellation; it does not implement matching or FIFO execution at a price. Those would need to be added before using it as a matching engine.

### Next steps

1. Run the profiling script on a Linux machine with hardware counters to check the simulated cache and branch results.
2. Try concentrated prices and heavier cancellation, rather than only the current benchmark distribution.
3. Measure runs without a capacity hint to see the effect of growth and rehashing.

## 8. Saved experiments

The hash-layout experiment in `results/macos/experiment_hash_grouping.txt` compares hashing the whole id with grouping four consecutive ids into one cache line. The file contains five alternating rounds; the table shows medians:

| | add ns (per call) | cancel ns (per call) | total ms |
|---|---|---|---|
| Whole id hashed (first version) | 100.9 | 97.9 | 49.8 |
| Ids grouped four to a cache line (final) | 36.2 | 100.8 | 45.6 |

Grouping reduced the individual add time more than the whole-workload time. One possible explanation is that consecutive operations can overlap memory accesses in the whole-workload pass. The timing results alone do not establish that explanation.

The workload-size experiment is in `results/macos/experiment_scaling.txt`. Each entry is the median of five rounds:

| Operations | Most orders resting | A (ns/op) | B (ns/op) | C (ns/op) |
|---|---|---|---|---|
| 100,000 | 50,000 | 322 | 181 | 25.8 |
| 300,000 | 150,000 | 274 | 143 | 29.1 |
| 1,000,000 | 501,000 | 275 | 139 | 39.4 |
| 3,000,000 | 1,502,000 | 302 | 210 | 56.5 |

C's time per operation increased as its order table grew. B also slowed down at 3 million operations. Larger storage putting more pressure on the cache is a plausible reason, but these runs do not isolate cache size from every other effect.
