# Order Book - Performance Report

Kartik - 24BCS10259 - kartik.24bcs10259@sst.scaler.com

## Setup

My laptop is a Mac, so perf does not work there. I first tried a Linux VM in podman and then an x86 Azure VM, but both showed only `task-clock` and `page-faults` (`no PMU driver, software events only` in dmesg). In the end I used an **Azure Standard_B2ps_v2** VM:

- CPU: ARM Neoverse-N1, 2 vCPU, L1d 64KB/core, L2 1MB/core, L3 32MB
- Ubuntu 24.04, kernel 7.0.0-1017-azure, g++ 13.3.0, `-std=c++17 -O2 -g`

The Azure ARM image disables the PMU driver on the kernel command line (`initcall_blacklist=arm_pmu_acpi_init`). I removed that from grub and rebooted, and then the hardware counters worked.

Counters that did **not** work:
- `dTLB-load-misses`: `<not supported>`. I also tried the raw ARM events `dtlb_walk` and `l1d_tlb_refill`, and they read 0, same for `l2d_cache_refill`. The hypervisor probably only passes some events through.
- `cache-misses`: on this ARM machine perf maps it to the same event as `L1-dcache-load-misses` (the numbers are identical), so it is really L1D misses, not LLC misses.

Things to keep in mind when reading the numbers:
- `perf stat` measures the whole process, which means 5 benchmark runs + generating the workload + all the `steady_clock::now()` calls. The ns/op numbers come from the program itself (median of 5).
- Every op is timed with two `steady_clock::now()` calls. In `perf report` for C, about 60% of the samples are in `[vdso]` (clock_gettime). So there is a floor of roughly **30 ns** per timed op from the timer itself. That's why get_bbo shows ~31 ns for all three. The real BBO cost is a few ns.

## Results table

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 302.62 | 294.95 | 86.11 |
| ns / add | 221.2 | 172.1 | 40.0 |
| ns / cancel | 500.2 | 637.8 | 83.1 |
| ns / BBO | 31.4 | 31.1 | 30.8 |
| IPC | 0.77 | 0.80 | 1.30 |
| Branch misses (%) | 4.86% | 10.24% | 1.49% |
| L1-dcache-load-misses | 55,546,289 | 109,953,475 | 6,296,438 |
| Cache misses | 55,546,289 (same event as L1D) | 109,953,475 | 6,296,438 |
| Page faults | 20,059 | 37,325 | 14,037 |
| dTLB-load-misses | not supported | not supported | not supported |

ns/op = median of 5 runs, 1M ops, seed 42. Counters = `perf stat -r 5` (each run of `./bench` does 5 runs internally).
IPC = instructions / cycles. Branch miss % = branch-misses / branches.

Raw files are in `results/` (`bench_*.txt`, `perf_stat_*.txt`, `perf_report_*.txt`).

## perf stat output

### A
```
 Performance counter stats for './bench --impl A' (5 runs):

           1796.78 msec task-clock                                                              ( +-  0.16% )
        5376791862      cycles                                                                  ( +-  0.15% )
        4126291856      instructions                                                            ( +-  0.01% )
         844919169      branches                                                                ( +-  0.01% )
          41052574      branch-misses                                                           ( +-  0.02% )
          55546289      cache-misses                                                            ( +-  0.04% )
          55546289      L1-dcache-load-misses                                                   ( +-  0.04% )
   <not supported>      dTLB-load-misses
             20059      page-faults

       1.800331864 +- 0.002774836 seconds time elapsed  ( +-  0.15% )
```

### B
```
 Performance counter stats for './bench --impl B' (5 runs):

           1513.81 msec task-clock                                                              ( +-  1.17% )
        4527617395      cycles                                                                  ( +-  1.17% )
        3603292802      instructions                                                            ( +-  0.01% )
         688812255      branches                                                                ( +-  0.01% )
          70532616      branch-misses                                                           ( +-  0.31% )
         109953475      cache-misses                                                            ( +-  0.29% )
         109953475      L1-dcache-load-misses                                                   ( +-  0.29% )
   <not supported>      dTLB-load-misses
             37325      page-faults                                                             ( +-  0.00% )

       1.514938946 +- 0.017749936 seconds time elapsed  ( +-  1.17% )
```

### C
```
 Performance counter stats for './bench --impl C' (5 runs):

            499.79 msec task-clock                                                              ( +-  1.09% )
        1496626562      cycles                                                                  ( +-  1.10% )
        1950236991      instructions                                                            ( +-  0.01% )
         338012020      branches                                                                ( +-  0.01% )
           5051277      branch-misses                                                           ( +-  1.39% )
           6296438      cache-misses                                                            ( +-  0.22% )
           6296438      L1-dcache-load-misses                                                   ( +-  0.22% )
   <not supported>      dTLB-load-misses
             14037      page-faults                                                             ( +-  0.00% )

       0.501376429 +- 0.005525780 seconds time elapsed  ( +-  1.10% )
```

### perf record (top symbols, `perf report --no-children`)

```
A:  20.75%  OrderBookA::cancel_order
    17.19%  run_once<OrderBookA>          (add_order got inlined here)
    12.66%  std::map<...>::operator[]
    ~16%    [vdso] clock_gettime
    ~11%    std::_Hashtable / _Map_base   (unordered_map find/insert/erase)
     2.3%   malloc + free

B:  29.71%  OrderBookB::cancel_order      (binary search over orders inlined)
    13.98%  std::__lower_bound<PriceLevel...>
     5.70%  ask_less, 5.10% bid_less      (comparator not inlined, called via fn pointer)
    ~19%    [vdso] clock_gettime
     2.87%  OrderBookB::add_order

C:  ~60%    [vdso] clock_gettime + steady_clock::now
    16.09%  run_once<OrderBookC>          (all book code inlined here)
     3.87%  make_workload
```

## 11.1 Which one is fastest?

C is the fastest for add (40 ns vs 172 / 221) and cancel (83 ns vs 500 / 638). Its total time is about 3.5x lower than A and B. For get_bbo all three are basically equal (~31 ns), and that is just the timer overhead. All three keep the best price somewhere they can read it in O(1) (`map.begin()`, `vector.back()`, cached int), so there is no real difference there.

Why C is faster:
- add is just `orders[id] = ...`, `level_qty[tick] += qty` and one compare. No search, no allocation, no rebalancing.
- cancel is one array index to find the order and one array index to find the level. The rescan when the best level empties almost never happens here because with ~250k orders per side every tick has lots of orders on it.
- `level_qty` is only 80KB, so the hot part of it stays in L1/L2. In A every level is a separate tree node somewhere in the heap.
- C executes about half the instructions of A and still gets a much better IPC (1.30 vs 0.77).

Between A and B: B is faster at add because once all 5000 levels exist (which happens quickly), adding is a binary search on a contiguous vector plus `push_back` of the order, with no malloc. A does a hash insert (malloc a node) plus `map::operator[]` (walk ~12 levels of a red-black tree). But B is the slowest at cancel. It has to binary search the orders vector, which is about 700k elements * 32 bytes (with padding) = ~22MB. That's ~20 steps and almost every step is a cache miss, plus a branch that is basically random. Overall A and B end up close in total time, because there are 3.5x more adds than cancels.

## 11.2 Branch prediction

B has the most branch misses: 10.2% of branches, 70M in total, vs 4.9% for A and 1.5% for C.

The cause is binary search. Each `lower_bound` step does `if (mid < key) go right else go left`, and with random prices and random ids that's a coin flip, so the predictor gets ~50% of them wrong. B does two of these on every cancel (orders, ~20 steps, and levels, ~12 steps) and one on every add. The comparators `bid_less`/`ask_less` also show up as separate functions in perf, so they were called through a function pointer and not inlined, which adds more branches.

A also does a tree walk (same left/right coin flip at each node), plus hash bucket chains, so it's in the middle. C has very few data-dependent branches: `if (tick > best_bid)` is almost always false because best_bid is already at 99.99, and `if (level_qty == 0)` on cancel is almost always false. These are easy to predict.

## 11.3 Cache behaviour

B has the most L1D misses (110M), then A (56M), then C (6.3M).

- **A** follows pointers and allocates lots of small objects. Every map level is a separate heap node, and every order is a separate hash node with `new`. A tree walk is ~12 dependent loads to random addresses, and the CPU can't start the next load until the current one comes back. The hash table is also big (1M+ buckets), so finding an order is a random access too.
- **B** uses contiguous memory, which sounds good, but binary search over a ~22MB array jumps around: first step in the middle, then 1/4, and so on. Only the last few steps hit the same cache lines. So it gets ~20 cache misses per cancel. The level vectors are small (5000 * 16 bytes = 80KB per side) so they are fine. Inserting or erasing a level shifts the vector, but that's rare after warmup.
- **C** is mostly contiguous. `level_qty` is 80KB total and the order vector is indexed directly. A cancel picks a random id, so it's one miss for the order and then usually a hit for the level. No pointers, no small allocations, no search.

Cache misses matter because a miss to L2 or RAM is tens to hundreds of cycles, during which the CPU just waits. That's why A and B have IPC under 1 and C gets 1.3. C's IPC would probably be even higher without the clock_gettime calls.

## 11.4 Page faults

B has the most page faults (37k), then A (20k), then C (14k).

A big part of the count is the same for everyone: the workload vector (1M * 32 bytes = 32MB) is created once and gets faulted in. So C's 14k is mostly baseline.

B reserves `1<<20` orders * 32 bytes = 32MB for every run, and since it never erases orders it really touches ~22MB of it. Big vectors like that are allocated with mmap and given back on free, so every run faults them in again. C's order vector is only 8 bytes per order (8MB), so it has much less to fault. A has 700k small nodes from malloc. They are packed in the heap and malloc reuses the memory between runs, so it lands in the middle.

So page faults mostly tell you how much new memory gets touched. B uses the most memory because of the dead orders it keeps around.

## 11.5 Production choice

I would choose **C**, with some changes.

- **Latency:** best for add and cancel, by a lot.
- **Predictability:** every op is a few array accesses, with no malloc and no tree rebalancing. The one bad case is when the best level empties and the rest of that side is very sparse, which can mean scanning up to 5000 ticks. That's bounded and cache friendly, but I would add a bitmap to make it faster (see 11.6). A has malloc in the hot path, and malloc can sometimes be slow. B has the O(L) vector insert/erase, and the orders vector reallocating (copying 20MB+) would be a huge latency spike.
- **Memory:** levels are a fixed 80KB. Orders are 8 bytes per id. That's good as long as ids are dense. In a real system ids are not 1, 2, 3, so I would map exchange ids to internal slot numbers with a free list (an object pool) so the vector doesn't grow forever.
- **Cache:** best of the three, see 11.3.
- **Complexity / maintainability:** A is the simplest and the hardest to get wrong, which is its big advantage. C is not much more code, but it has hidden assumptions: a fixed price band, dense ids, and qty stored as int. Those need to be documented and checked. If the price band changes, C needs to change too.

B is the one I would not deploy: its cancel is the slowest and it grows memory forever.

## 11.6 What I would try next

- Remove the per-op `steady_clock` overhead. Measure in batches, or use `rdtsc` / `cntvct_el0` directly, so the BBO numbers mean something.
- Keep a bitmap of non-empty ticks (5000 bits = 79 uint64) for C. Then finding the next best level after a cancel is a few `__builtin_ctzll` calls instead of a loop over the ticks.
- An object pool / free list for order slots in C, so ids can be anything (hash once on add → slot index) and memory is reused.
- For A: a pool allocator for the map and hash nodes to remove malloc/free from the hot path, or a flat open-addressing hash map instead of `std::unordered_map`.
- For B: erase cancelled orders in batches (compaction), so the orders vector doesn't keep growing. Use a lambda comparator so `lower_bound` gets inlined. Try a branchless binary search to cut down branch misses.
- Use smaller types: `int32` qty and ticks, and pack order data more tightly.
- Test with a more realistic workload where most orders are near the top of the book. In that case B's best-at-the-back layout and C's rescans behave differently than with uniform random prices.
