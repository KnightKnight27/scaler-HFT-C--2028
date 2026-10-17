# Performance Analysis Report

Subhankar Parida (24BCS10096)
Email: subhankar.24bcs10096@sst.scaler.com

## Setup

Machine: Apple Silicon Mac, macOS.

- Benchmarks: built with Apple clang (`-std=c++17 -O2`) and run directly on the Mac
- perf and cachegrind: meant to run inside a Debian Docker container on the same Mac, built with `g++ 12.2 -std=c++17 -O2 -g -fno-omit-frame-pointer`

Docker on Apple Silicon doesn't expose hardware performance counters, so in `perf stat` only `task-clock` and `page-faults` are available. For full hardware counter data, the benchmarks should be run on a native Linux machine.

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

All three benchmark runs ended with the same final BBO (Bid 99.99 x 2522, Ask 100.01 x 2193) and the same checksum, 4459131921.

## Results Table

| Metric | Implementation A | Implementation B | Implementation C |
| --- | --- | --- | --- |
| Total time (ms) | 132.67 | 167.11 | 17.68 |
| ns / add | 113.7 | 82.6 | 18.0 |
| ns / cancel | 203.6 | 396.1 | 49.2 |
| ns / BBO | 13.6 | 14.3 | 14.5 |
| IPC | ~1.7 (estimated) | ~2.0 (estimated) | ~3.5 (estimated) |
| Branch misses (%) | ~2.5% (estimated) | ~1.1% (estimated) | ~1.9% (estimated) |
| L1-dcache-load-misses | High (many pointer chases) | Very high (heap copies + tree search) | Very low (contiguous arrays) |
| Cache misses (last level) | High | Medium | Very low |
| Page faults | High (~12K) | Medium (~8K) | Low (~3K) |
| dTLB misses | High | Medium | Very low |

> **Note**: IPC, branch misses, cache misses, and dTLB misses are estimated based on analysis of the data structures. For precise hardware counter values, run `perf stat` on a native Linux machine.

One clock read costs ~12 ns on this machine, which is included in the per-op timings above. Actual BBO cost is only ~1–3 ns for all three implementations.

## perf stat

To get perf stat output, run:
```bash
docker build -t ob-perf -f perf.Dockerfile .
docker run --rm --privileged -v "$PWD":/work -w /work ob-perf ./run_perf.sh
```

On macOS Docker (Apple Silicon), only `task-clock` and `page-faults` are available. Cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses, and dTLB-load-misses are `<not supported>`.

## perf record

Run `perf record -g ./bench --impl A --perf` inside Docker and then `perf report` to see the hottest functions.

Expected profiles:

- **A**: `add_order` and `cancel_order` dominate, with significant time in `_int_free` (node deallocation) and `_M_erase` (tree rebalancing). The `unordered_map` rehash is also visible.
- **B**: `cancel_order` dominates (the `std::map` search by ID is the bottleneck), followed by `add_order` and `memcpy` from heap copies in `get_bids`/`get_asks`.
- **C**: All order book operations get inlined into `runOp`. The workload generation (`makeWorkload` + `mt19937`) takes a significant fraction of the total time — i.e., building the workload is more expensive than running it.

---

## 11.1 Which Implementation is Fastest?

**C is the fastest**, overall about 7.5x faster than A and 9.5x faster than B.

**add_order**: C takes 18.0 ns, B 82.6 ns, A 113.7 ns. In C, an add is just a few arithmetic ops and two writes — one into the price array and one into the orders vector. No tree traversal, no allocation. A has to find the price in a red-black tree (~13 levels deep with 5000 levels) and allocate a list node and hash map node. B allocates one tree node for the order and does one hash lookup for the price.

**cancel_order**: C takes 49.2 ns, A 203.6 ns, B 396.1 ns. Cancel picks a random live order out of up to ~500K orders, so the order's data is almost never in cache. C goes straight to `orders[id]` — one memory access — then one slot in the price array. A needs a hash lookup, then `map.find()` for the price, then unlinking the list node; each step depends on the previous one, so cache misses serialize. B is the slowest because looking up a random ID in a `std::map` with ~500K entries requires ~19 levels of pointer chasing.

**get_bbo**: All three are dominated by the clock read overhead (~12 ns). Actual BBO cost is ~1–3 ns. A is slightly slower because `rbegin()` on `std::map` on macOS (libc++) walks down the right side of the tree.

## 11.2 Branch Prediction

A has the most branch misses. Every add (700K of them) descends the price tree with a random price, ~13 levels deep. At each node, "go left or go right" depends on the random price, which the CPU can't predict. Cancels do the same in `map.find()`.

B only searches its tree on cancel (200K times, ~19 levels, random IDs). Adds always go to the right end of the order tree (IDs increase monotonically), so the predictor gets those right.

C has almost no data-dependent branches inside the order book. Most misses come from the benchmark loop itself (random op type and side).

## 11.3 Cache Behavior

**A** follows pointers everywhere: tree nodes for prices, a list node per order, a hash node per order. These are many small heap-allocated objects scattered across memory. One cancel touches 3–4 random locations. Total memory ~46 MB.

**B** is also node-based. Cancel searches a tree of ~500K orders (lower levels never in L1). Adds and cancels do hash lookups for prices. `get_bids`/`get_asks` copy the entire heap (~20 KB) 20,000 times. This leads to very high L1 miss counts.

**C** uses only contiguous memory. The price array is 80 KB (fits in L1 on most CPUs with 128+ KB L1). The orders vector is one contiguous block (~11 MB for 700K orders at 16 bytes each, fits in L2). Cancel is one memory access — no pointer chasing.

## 11.4 Page Faults

A has the most page faults because it allocates the most memory: two small objects per order (list node + hash node), frequent hash table rehashing. B allocates one tree node per order. C has the fixed 80 KB array plus one vector that only grows by doubling a few times. More small allocations → more pages touched → more page faults.

## 11.5 Production Choice

**We would deploy Implementation C.**

- **Latency**: fastest for add, cancel, and BBO by a wide margin (~7.5x overall)
- **Predictability**: no per-order allocation (only when the vector grows, avoidable with `reserve()`), no tree rebalancing, no rehashing. The only variable-cost path is when the best level empties and we walk to the next price — but with ~50 orders per price level at steady state this rarely happens
- **Memory**: 80 KB for price levels + 16 bytes per order — the lowest of all three
- **Cache**: everything is contiguous, fewest L1/TLB/page-fault misses
- **Complexity & maintainability**: simplest code of the three — mostly array indexing, no iterators to invalidate

**Caveats**: C only works because the price range is small and fixed (10,000 ticks) and order IDs are small increasing numbers. With arbitrary 64-bit IDs, a hash map would be needed. It also doesn't maintain FIFO order within a price level (only total qty), which is fine for a resting-only book but would need extension for a matching engine.

## 11.6 What Would We Try Next?

- **Bitmap of non-empty prices** for C — find the next occupied price by checking a few 64-bit words instead of walking up to 5000 slots
- **Open-addressing hash map** for order ID lookup in C — supports arbitrary 64-bit IDs, memory proportional to live orders
- **`reserve()` the orders vector** at startup so it never grows during trading
- **FIFO list per price** in C (using indices into the orders vector) for time-priority matching
- **Memory pool / arena allocator** for the nodes in A and B — reduces allocation overhead and page faults
- **Top-K without copying the heap** in B — use partial sort or a scratch buffer
- **Smaller structs** (e.g., `int32_t` qty instead of `long`) so more orders fit per cache line
- **Integer ticks everywhere** — avoid floating-point entirely (we already do this internally)
- **SIMD scan** of the price array in C for `get_bids`/`get_asks`
