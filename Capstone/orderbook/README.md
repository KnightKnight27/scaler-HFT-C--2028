# Low-Latency Order Book — Capstone

Name: Tanishka Mangure  
Roll Number: 24bcs10264  

Single-instrument resting order book (no matching), implemented three ways
and benchmarked with the same 1M-op workload. C++17, stdlib only.

## Files

| File | What it is |
|---|---|
| `common.hpp` | Shared types (`Side`, `Level`, `BBO`) + tick helpers |
| `book_a.hpp` | Impl A: `std::map` levels + `unordered_map` id lookup |
| `book_b.hpp` | Impl B: `unordered_map` levels + linear scan for best price |
| `book_c.hpp` | Impl C: fixed price-indexed arrays + cached best bid/ask |
| `bench.cpp` | One benchmark program (`--impl A/B/C`) + `--verify` correctness check |
| `Makefile` | Build with `g++ -O2 -std=c++17 -Wall` |
| `EXPLAIN.md` | Plain-English explanation of everything (viva prep) |
| `results/` | Saved outputs: verify + bench + perf + gprof |

## Build

```sh
make
```

Needs only `g++` (C++17). No external libraries.

## Correctness check

```sh
./bench --verify
```

Runs the same random workload through A, B and C and compares BBO +
top-10 bids/asks every 1000 ops, plus edge cases (empty book, unknown and
double cancel, shallow depth). Output saved in `results/verify.txt`.

## Benchmark

```sh
./bench --impl A
./bench --impl B
./bench --impl C
```

1,000,000 ops, seed 42 (70% add / 20% cancel / 10% BBO, depth-10 snapshot
every 100 ops). Flags: `--ops N`, `--seed N`. Each run prints per-op-type
timings, final BBO + checksum (so the work can't be optimized away), and
peak RSS / page faults from `getrusage`. Outputs saved in `results/bench_*.txt`.

## Interface change (documented as required)

Public API keeps `double price`, but internally prices are integer ticks
(`tick = round(price * 100)`). Tick size is fixed at 0.01, so this is
exact — it avoids float rounding problems and makes C's array indexing
possible. Conversion helpers live in `common.hpp`.

## Data-structure choices

| | A: `std::map` | B: hash + scan | C: price-indexed array |
|---|---|---|---|
| Orders stored | `unordered_map<id, info>` | `unordered_map<id, info>` | `unordered_map<id, info>` |
| Levels stored | `map<tick, qty>` (sorted) | `unordered_map<tick, qty>` | `vector<long>` indexed by tick |
| Best bid/ask found | tree ends (`rbegin`/`begin`) | full scan of all levels | cached variables |
| Order found on cancel | hash lookup by id | hash lookup by id | hash lookup by id |
| `add_order` | O(log M) | O(1) avg | O(1) |
| `cancel_order` | O(log M) | O(1) avg | O(1) avg |
| `get_bbo` | O(1) | O(M) | O(1) |
| `get_bids`/`get_asks` | O(depth) | O(M·depth) | O(depth + skipped empties) |

M = live price levels (up to 10,000 here).

## Correctness results

```
Edge cases (empty BBO, unknown/double cancel, shallow depth): PASS
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

All three also agree on the final state (checksum `304767386`,
final BBO `Bid: 99.99 x 3059 / Ask: 100.01 x 3152`). See `results/verify.txt`.

## Performance results

Machine: AMD Ryzen 5 6600H, 14 GB RAM, Linux. Built with `g++ -O2
-std=c++17 -Wall` (g++ 16.2.1). Workload: 1M ops, seed 42 → 701,221 adds /
198,995 cancels / 99,784 BBO queries, plus a depth-10 snapshot every 100 ops.

Per-op timings: median of 5 `bench` runs (`steady_clock`). Counters: mean
of 5 runs from real `perf stat -r 5` with the spec's exact event list.
Raw outputs: `results/perf_{A,B,C}.txt`.

| Metric | A (map) | B (hash+scan) | C (array) |
|---|---|---|---|
| Total time (ms) | 292.81 | 18387.49 | 136.88 |
| ns / add | 239.4 | 206.8 | 85.0 |
| ns / cancel | 474.4 | 589.0 | 251.0 |
| ns / BBO | 24.4 | 103765.2 (~104 µs) | 23.6 |
| ns / depth snapshot (10+10) | ~330 | ~783,000 (~0.78 ms) | ~178 |
| task-clock (ms) | 418.89 | 18555.43 | 355.47 |
| cycles | 1,671,325,866 | 80,903,192,991 | 1,354,690,528 |
| instructions | 1,231,510,976 | 22,203,959,932 | 1,143,051,092 |
| IPC | 0.74 | 0.27 | 0.84 |
| branches | 230,750,507 | 5,102,223,627 | 202,628,648 |
| branch-misses | 9,474,510 | 5,217,853 | 3,135,574 |
| Branch misses (%) | 4.11 | 0.10 | 1.55 |
| L1-dcache-load-misses | 22,501,248 | 2,646,565,132 | 12,550,277 |
| Cache misses | 10,476,146 | 1,599,523,977 | 6,095,288 |
| dTLB-load-misses | 1,257,563 | 2,568,373 | 1,165,261 |
| Page faults | 20,071 | 20,012 | 19,972 |
| Peak RSS (KB) | 64,320 | 64,416 | 64,496 |

Note: task-clock includes workload generation, so it's higher than the
bench's own ops-loop total. The gap is setup work, confirmed by `gprof`.

## Hotspot profile (`gprof`, stands in for `perf record`)

Outputs: `results/gprof_{A,B,C}.txt`.

- **A:** `add_order` 36%, `cancel_order` 24% — tree ops dominate.
- **B:** 98% inside `run_bench` — the program is one giant query scan (inlined loops). `add`/`cancel` under 0.3% each.
- **C:** book ops shrink (`add` 10% + `cancel` 15%); workload generation (~25%) becomes the biggest item — the book got so cheap test setup costs more than the thing tested.

## Performance analysis

### 11.1 Fastest implementation

- **`add_order`: C (85 ns)** — one hash insert + one array write, no tree rebalancing, no per-level allocation. A pays red-black insert (~239 ns); B's add is a hash insert (~207 ns) but hashing + bucket chasing costs more than C's direct index.
- **`cancel_order`: C (251 ns)** — hash lookup + array write; the cached-best repair walk almost never triggers. A pays tree delete (~474 ns); B pays two hash ops + erase (~589 ns).
- **`get_bbo`: A and C tie (~24 ns)** — both are pointer/variable reads. B takes ~104 µs because it scans all ~10,000 levels per query; BBO is 10% of the workload, so B's total (18.4 s) is ~135× worse than C's.

Why C wins overall: fewest instructions per op (~1.14B vs A's ~1.23B vs B's ~22.2B), least memory touched (240 KB arrays stay cache-resident), zero per-level allocations.

### 11.2 Branch prediction

**A has the most branch misses: 9.47M (4.11% miss rate)**, then B (5.22M,
0.10%), then C (3.14M, 1.55%). Surprise: B has the *lowest* miss rate.
B's scan loops are long monotonous loops (back-edge taken 10,000× in a row)
— the predictor loves that. A's misses come from red-black tree comparisons:
data-dependent go-left/go-right on random keys, the hardest pattern to
predict. C's hot path is nearly branchless (array index + one min/max
compare).

### 11.3 Cache behavior

**B is by far the worst: 1.60B cache misses, 2.65B L1 misses** (~150× A,
~260× C). Every query streams whole hash tables (bucket array + heap nodes)
— ~10k pointer hops per BBO, ~200k per depth snapshot, nothing reused.
Its IPC of 0.27 shows the CPU stalled on memory most of the time.

**A (10.5M misses, IPC 0.74):** each level is a separately allocated tree
node; inserts/deletes chase pointers to random heap addresses.

**C (6.1M misses, IPC 0.84 — best):** contiguous `vector<long>` slots,
sequential access in repair walks / depth snapshots, 240 KB fits in L2/L3.
C beats A 2–3× per op because the constant is cache lines, not Big-O.

### 11.4 Page faults

All three: ~20k faults, 0 major, ~64 MB peak RSS — practically identical.
The book is small; first-touch at startup causes minor faults; nothing
pages out on 14 GB RAM. The id `unordered_map` (~500k live orders at peak)
dominates memory equally in all three, so RSS barely differs. Conclusion:
designs differ in *access pattern*, not footprint.

### 11.5 Production choice

**C**, with one reservation. Latency best on every op type, and most
*predictable* — add/BBO/depth are flat O(1) array ops with the lowest miss
counts; A's tree ops vary with depth; B degrades as the book fills.
Memory: fixed ~240 KB + id map; zero per-level allocation = zero allocator
jitter on the hot path. Trade-off: tick-indexing + best-repair logic is
subtler than `std::map` and only works because the price range is bounded
and known upfront. For this instrument that constraint is given, so C is
the right call; for unbounded prices, fall back to A (same logical
interface, no caller changes).

### 11.6 What would you try next?

1. Integer ticks in the public API too (kills `llround` per add).
2. Faster id lookup — ids are dense and never reused; a `vector` indexed by id makes lookup nearly free (cancel is now C's slowest op).
3. Pre-allocation (`reserve()` the id map ~700k adds) to avoid rehash pauses.
4. Combined top-of-book call returning bids+asks in one pass.
5. Huge pages / pinning — only if major faults appear; they don't here.

## Profiling with `perf`

`perf` counters (spec's exact event list, 5 runs each); outputs in
`results/perf_*.txt`:

```sh
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A
```

Repeat for B and C. `perf stat` prints to stderr, so capture with `> file 2>&1`. All 8 counters work on this machine (`perf_event_paranoid` is 2). Install on Arch/Omarchy:

```sh
sudo env OMARCHY_ALLOW_DIRECT_PACMAN=1 pacman -S perf
```

For the `perf record` part (where time is spent), `gprof` was used:

```sh
g++ -O2 -pg -std=c++17 -o bench_pg bench.cpp
./bench_pg --impl A && gprof bench_pg gmon.out
```

Outputs in `results/gprof_*.txt`.
