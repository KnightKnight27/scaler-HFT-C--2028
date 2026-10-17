# Capstone Report — Low-Latency Order Book

**Om Malviya · om.24bcs10448@sst.scaler.com · 2024bcs10448**

Machine: MacBook Air, Apple Silicon (arm64), macOS 15 (Darwin 25.2.0), GCC 15.1.0,
`-O2 -std=c++17`. Workload: 1,000,000 ops, seed `mt19937(42)`, ~70% add / 20% cancel /
10% BBO, plus `get_bids(10)`+`get_asks(10)` every 100 ops. Timing = median of 5 runs
(`steady_clock`). All three implementations produce the **identical** final BBO
(`Bid 99.99 x 1981`, `Ask 100.01 x 2573`) and the **identical checksum `496338337`**.

> **On `perf`:** Linux `perf` is not available on macOS/arm64. Per the assignment FAQ,
> the hardware-counter rows that require `perf` (branch-misses, cache-misses,
> L1-dcache-load-misses, dTLB-load-misses) are marked *unavailable here*; `perf.sh` is
> included to produce them on a Linux lab machine. What *is* measurable locally
> (`/usr/bin/time -l`) — instructions, cycles → **IPC**, peak memory, page activity — is
> reported below and is enough to explain the behaviour.

## Correctness

```
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

Also verified: cancel of unknown / already-cancelled id returns `false` (no crash),
empty-book `get_bbo()` reports no bid/ask, and `get_bids(10)` on a 3-level book returns 3.

## 10. Results Table

| Metric | **A — std::map** | **B — unordered_map** | **C — array+bitmap** |
|---|---:|---:|---:|
| Total time (ms, median of 5) | 117.18 | 1323.77 | **30.79** |
| ns / add | 72.7 | 31.3 | **13.1** |
| ns / cancel | 246.4 | 302.4 | **27.0** |
| ns / BBO | 11.5 | 12.6 | 11.9 |
| IPC (instr ÷ cycles) | 2.16 | 0.64 | **2.91** |
| Instructions retired | 1.09 B | 2.97 B | 0.62 B |
| Cycles elapsed | 0.51 B | 4.61 B | 0.21 B |
| Branch misses (%) | *perf (Linux) only* | *perf (Linux) only* | *perf (Linux) only* |
| L1-dcache-load-misses | *perf (Linux) only* | *perf (Linux) only* | *perf (Linux) only* |
| Cache misses | *perf (Linux) only* | *perf (Linux) only* | *perf (Linux) only* |
| Page faults (hard) | 44 | 2 | 2 |
| Page reclaims (minor) | 4965 | 5036 | 4507 |
| Peak memory (MB) | 62.3 | 62.8 | **54.2** |

Instructions/cycles/IPC/memory/faults are whole-process `/usr/bin/time -l` on a single
run; workload generation is identical across A/B/C, so the differences are the order
books. (`ns/BBO` ≈ 11–13 ns for all three is essentially the `steady_clock::now()`
measurement floor — `get_bbo` is O(1) and otherwise near-free in every implementation.)

## 11. Performance Analysis

### 11.1 Which implementation is fastest?
- **add_order:** **C (13.1 ns)** < B (31.3) < A (72.7). C does one array write plus maybe a
  single bitmap set and one vector store — all contiguous, no allocation, no rebalancing.
  B pays hashing cost and occasional rehash; A allocates a red-black-tree node and
  rebalances on most inserts.
- **cancel_order:** **C (27.0 ns)** ≪ A (246.4) < B (302.4). C indexes the order array
  directly by id and decrements — O(1). A does a hash lookup then an O(log L) `std::map`
  erase with rebalancing. B does a hash lookup and, whenever the cancel empties the best
  level, an **O(L) rescan** of all scattered keys to find the new best.
- **get_bbo:** all three are O(1) and sit at the ~11–13 ns clock floor; C/A/B are within
  noise of each other.

C wins because its hot paths are contiguous index arithmetic with no allocation, no
pointer chasing, and a branch-light bit-scan for the best price.

### 11.2 Branch prediction
*(Exact `branch-misses` need Linux `perf`; IPC is the local proxy.)* B's IPC of **0.64**
means the CPU retires well under one instruction per cycle — heavily stalled — while C
runs at **2.91**. Likely mispredict sources: A's red-black-tree traversal takes a
data-dependent `<` branch at every node (classic mispredictor); B's `partial_sort` in
`get_bids/get_asks` and its best-recompute loop run many data-dependent comparison
branches over unordered data. C avoids per-element branches almost entirely — the best
price is found with `clz`/`ctz`, and the only hot branch (`qty == 0`) is highly
predictable. Expected branch misses: **B > A > C**.

### 11.3 Cache behavior
*(Exact cache-miss counts need Linux `perf`; design + IPC + cycle counts explain it.)*
- **A** allocates a separate heap node per price level and chases pointers through the
  tree → scattered access, many small objects.
- **B** scatters level nodes across hash buckets and, critically, its `get_bids/get_asks`
  touch **every** level (O(L)) every 100 ops and its best-recompute scans all keys — huge
  scattered memory traffic. This shows up as **4.6 B cycles vs 0.5 B for A** for the same
  1 M ops: B is memory-bound.
- **C** uses two contiguous arrays + one contiguous order vector + 79-word bitmaps →
  sequential, prefetch-friendly access, no per-level allocation (IPC 2.91).

Expected cache misses: **B ≫ A > C**.

### 11.4 Page faults
B touches the most distinct pages (scattered nodes for ~700 k orders plus level maps,
repeatedly scanned) and shows the most minor reclaims (5036) and the largest footprint
(62.8 MB). C has the smallest footprint (54.2 MB) and fewest reclaims (4507) because its
data is compact and contiguous. Hard page faults are ~0–2 (A's 44 is cold-start noise) —
the working set fits in RAM, so this mostly reflects **allocation scatter and footprint**,
where C is best.

### 11.5 Production choice
I would deploy **C (dense arrays + bitmap)** — and not only because it has the lowest
average latency:
- **Predictability (most important for HFT):** C has no O(L) spikes, no rehash, no tree
  rebalance, so its tail latency is the flattest. A always pays O(log L); B has O(L)
  blow-ups on best-emptying cancels and on every depth query — unacceptable tails.
- **Memory:** bounded, fixed footprint (two 5000-slot arrays + bitmaps + order pool), no
  per-order/per-level heap churn.
- **Cache behavior:** contiguous, highest IPC.
- **Complexity/maintainability:** slightly more code (bit tricks), but small and
  self-contained; A is the easiest to read if simplicity were the priority.
- **Caveat:** C relies on a bounded price grid (10,000 fixed levels here) and dense ids.
  For an unbounded price range that assumption breaks and a hybrid (map, or a windowed
  array around the touched range) would be needed. For this single-instrument spec, C is
  the right call.

### 11.6 What I would try next
- An **object pool / free-list** for order records (recycle ids) to kill the `vector`
  growth/copy and bound memory exactly.
- A **two-level bitmap** (a summary word over the 79 words) so best-price find is one
  `clz`/`ctz` instead of a ≤79-word scan.
- **Tighter packing:** 32-bit level quantities and a packed order record so more fits per
  cache line.
- **Prefetch** the order slot on cancel; branch-reduce the hot paths.
- Have `get_bids` write into a caller-provided buffer to avoid allocating a `vector`.
- Replace per-op `steady_clock` with `rdtsc`/`perf` to remove the ~11 ns measurement floor.
- Run `perf.sh` on Linux to confirm the branch-miss / cache-miss predictions above.
