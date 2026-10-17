# Capstone: Low-Latency Order Book

email: "rudhar.24bcs10143@sst.scaler.com"

roll_no: "10143"

Name: Rudhar Bajaj

A resting order book for one instrument, built three times with different data structures. All three use the same API, are checked against each other by one correctness test and are timed by one benchmark program. I profiled them with Linux perf and cachegrind (in Docker) and with the hardware counters on my Mac.

| | Design | Total time, 1M ops |
|---|---|---|
| A | `std::map` price levels + `std::unordered_map` orders | 145.3 ms |
| B | binary heaps with lazy deletion + hashed levels + `std::map` orders | 151.3 ms |
| C | flat tick array + two-level occupancy bitmap + my own open-addressing hash | 14.6 ms |

## 1. Build and run

C++17, standard library only, built with `-O2`.

```bash
make                      # or: g++ -std=c++17 -O2 -Iinclude src/bench.cpp -o bench

./bench --verify          # correctness test: A, B and C must agree
./bench --impl A          # benchmark one implementation (5 runs, median)
./bench --impl B
./bench --impl C
./bench --impl all        # all three, one after another
```

Other options are `--runs N`, `--ops N` and `--seed N`. `--total-only` does a single pass with no per-op timers, which is what I use under perf. `--impl none` only generates the workload and is used as a baseline.

Linux profiling (perf stat, perf record and cachegrind for all three):
```bash
scripts/perf_linux.sh      # on Linux; writes results/
# on a Mac, inside Docker:
docker run --rm --platform linux/arm64 --privileged -v "$PWD":/ob -w /ob ubuntu:24.04 \
  bash -c 'apt-get update -qq && apt-get install -y -qq g++ make linux-tools-generic valgrind >/dev/null && scripts/perf_linux.sh'
```

Files:

| Path | What |
|---|---|
| `include/common.hpp` | `Side`, `Level`, `BBO`, price/tick conversion, band checks |
| `include/book_a.hpp`, `book_b.hpp`, `book_c.hpp` | the three implementations |
| `include/workload.hpp` | repeatable workload generator (mt19937, seed 42) |
| `src/bench.cpp` | benchmark, `--verify`, edge-case tests |
| `scripts/perf_linux.sh` | perf stat, perf record and cachegrind for A, B and C |
| `results/` | raw output of every run quoted in this README |

## 2. Interface changes

The API is the one from the brief: `add_order(id, side, double price, qty)`, `cancel_order`, `get_bbo`, `get_bids(depth=10)`, `get_asks(depth=10)`. What I changed or added:

- Prices are integer ticks inside every book. The price is converted once on entry with `tick = llround(price * 100)` (99.99 becomes 9999, 100.01 becomes 10001), and converted back to `double` only when a `Level` or `BBO` is returned. I used `llround` and not a cast because `100.01 * 100` comes out as `10000.9999...`.
- `add_order` returns `void`, so invalid orders are silently ignored: a price outside the side's band (bids 50.00 to 99.99, asks 100.01 to 150.00), a price that is not a multiple of 0.01, a quantity of 0 or less, or an id that is already in use. The correctness test covers each of these.
- `get_bbo()` is `const` in all three. For B this works because `cancel_order` cleans dead ticks off the top of the heap (see below).
- `BookC` takes an optional constructor argument, the expected number of orders (default 2^20), to pre-size its hash table.

## 3. The three designs

How each one stores orders and levels, finds the best price and finds an order to cancel:

| | A | B | C |
|---|---|---|---|
| Orders | `std::unordered_map<id, {side, tick, qty}>`, one heap node per order | `std::map<id, {side, tick, qty}>`, a red-black tree | my own open-addressing table, a flat array of 16-byte `{id, tick, qty}` slots |
| Price levels | `std::map<tick, qty>` per side, one tree node per level | `std::unordered_map<tick, {qty, in_heap}>` per side | `qty[15001]`, one `int64` per possible tick (120 KB, contiguous) |
| Best bid/ask | `begin()` of the map (bids use `std::greater`) | top of a `std::priority_queue` (max-heap for bids, min-heap for asks) | cached `best_bid_`/`best_ask_`; when that level empties, a `clz`/`ctz` scan of the bitmap finds the next one |
| Cancel lookup | hash lookup (chained buckets) | tree search, O(log N) | Fibonacci hash, then linear probing in one array |

### A: standard containers
The textbook version. Each side is a sorted `std::map` from tick to total quantity, so the best level is `begin()` and depth is an in-order walk. Orders are in an `unordered_map`. Every level and every order is a separately allocated node.

### B: heaps with lazy deletion
Levels are in a hash map, which has no order, so each side also has a heap to track the best price.
- A tick is pushed onto the heap only if it is not already there (the `in_heap` flag), so there are no duplicates.
- When a level empties, its tick stays in the heap (lazy deletion).
- After a level empties, `cancel_order` pops dead ticks off the top until the top is a live level. So the top is always live and `get_bbo()` is just `top()`.
- `get_bids`/`get_asks` copy the heap and pop from the copy, skipping dead ticks, because a heap can't be walked in sorted order.
- Orders are in a `std::map` (a tree) and not a hash table like A, so cancel lookup is also different.

### C: my own design (price-indexed array + bitmap + flat hash)
- Levels: there are only 10,000 valid ticks, so the level for tick t is just `qty_[t]`. No search and no allocation.
- Next level: an occupancy bitmap has one bit per tick, and a 4-word summary marks which bitmap words are non-empty. Finding the next non-empty level below or above t takes at most two `clz`/`ctz` instructions and a few word loads, no matter how many levels there are.
- Best price: `best_bid_`/`best_ask_` are updated with one comparison on add, and only recomputed when the best level itself empties.
- Bids and asks share one bitmap. The bands don't overlap, so a bid search going down from 9999 or below never reaches an ask bit, and the other way round.
- Orders: my own hash table, a flat array of 16-byte slots holding `{id, tick, qty}`. It uses linear probing with Fibonacci hashing (multiply the id by 2^64/phi and keep the top bits) and keeps the load factor at 0.7 or below. Deletion uses backward shift, so there are no tombstones. The side isn't stored because a tick of 9999 or below is always a bid.
- Nothing is allocated after construction unless the table has to grow.

### Complexity
L = live price levels on a side (at most 5000), N = live orders, H = heap size including dead ticks, d = requested depth.

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | O(log L) map + O(1) avg hash | O(log N) map + O(1) avg hash + O(log H) push (new level only) | O(1) avg hash + O(1) array + O(1) bitmap |
| `cancel_order` | O(1) avg hash + O(log L) map | O(log N) map + O(1) avg hash + amortised O(log H) pops | O(1) avg hash + O(1) array + bounded bitmap scan |
| `get_bbo` | O(1) (`begin()`) | O(1) (`top()`) | O(1) (cached) |
| `get_bids(d)` / `get_asks(d)` | O(d) | O(H + d log H) (copies the heap) | O(d) |

For C, the bitmap scan reads at most 4 summary words and 2 bitmap words, because the tick range is fixed at 15,000, so it counts as O(1).

## 4. Correctness

`./bench --verify` does two things:

1. Edge cases from the brief, on each implementation: empty book gives `has_bid == has_ask == false`; cancelling an unknown id returns `false`; cancelling the same order twice returns `false` the second time; asking for 10 levels when only 3 exist returns 3; several orders at one price add up to one level; when the best level empties the BBO moves to the next level; band edges (50.00, 100.01, 150.00) are accepted; out-of-band, off-grid, zero-quantity and duplicate-id orders are ignored.
2. Lockstep comparison: the same workload runs through A, B and C side by side. After every op the three BBOs must match, and every cancel's return value must match. Top-10 bids and asks (price and quantity) are compared every 1 to 100 ops, and all levels on both sides are compared periodically and at the end. Results are compared pairwise (A-B, A-C, B-C), so a failure points at the implementation that is wrong. I use four workloads:
   - the benchmark workload itself (1M ops, seed 42)
   - narrow band, cancel-heavy (12 ticks per side), so the best level empties all the time
   - scrambled (non-sequential) order ids
   - drain, where cancels outnumber adds and the book keeps running empty

To check the test can actually fail, I put two bugs into C on purpose: not moving `best_bid_` when its level empties, and a wrong backward-shift condition in the hash table. Both were reported, and only C was blamed. A bug I put into B's heap cleanup made the verifier crash (an `unordered_map::at` exception) instead of reporting a mismatch, so that one was caught too, just less cleanly. The verifier also runs clean under `-fsanitize=address,undefined`.

Output ([results/verify.txt](results/verify.txt) on macOS; [results/verify_linux.txt](results/verify_linux.txt) is the same on Linux):
```
Running correctness test...

Edge cases (empty book, unknown/double cancel, insufficient depth, invalid input):
  A: ok   B: ok   C: ok

Lockstep comparison of A, B and C on identical random workloads:
  benchmark workload (seed 42)        1010000 ops,  1010000 BBO +   20000 top-10 compares, 5000 bid levels left: agree
  narrow band, cancel-heavy (seed 7)   303000 ops,   303000 BBO +  303000 top-10 compares,   12 bid levels left: agree
  scrambled ids (seed 2026)            303000 ops,   303000 BBO +   33000 top-10 compares, 5000 bid levels left: agree
  drain to empty (seed 99)             202000 ops,   202000 BBO +  202000 top-10 compares,    2 bid levels left: agree

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```
The benchmark also prints a checksum of everything each book returned. All three give `fb65302f2675591e`, on macOS and on Linux.

## 5. Benchmark

Workload, as in the brief:
- 1,000,000 ops from `std::mt19937(42)`: 70% add, 20% cancel, 10% `get_bbo`
- `get_bids(10)` and `get_asks(10)` every 100 ops
- side 50/50, quantity 1 to 100, price a random valid tick in the side's band
- cancels always pick a live order (the generator keeps a list of live ids and swap-removes each cancelled one)
- actual mix: 700,471 add, 199,305 cancel, 100,224 BBO, 10,000 depth queries

All ops are generated before timing starts, and the benchmark is templated on the book type, so there are no virtual calls in the timed loop. I used my own small range function on top of mt19937 instead of `std::uniform_int_distribution`. libstdc++ and libc++ implement that differently, so the same seed would otherwise give a different workload on Linux and macOS.

How it measures (each implementation: 1 warm-up, then 5 runs, median reported):
1. Total time: one pass with only the book calls inside the loop.
2. Per-op time: a second pass on a fresh book, with each op wrapped in a pair of `steady_clock::now()` calls and added to that op type's total. Its checksum has to match pass 1.
3. BBO block time: `get_bbo()` is cheaper than the clock, so I also time 1M back-to-back calls on the final book.

On Apple Silicon `steady_clock` moves in steps of about 41.7 ns (a 24 MHz counter) and a pair of `now()` calls costs about 29 ns. So any per-op number around 15 to 40 ns is mostly the clock. Only averages over many calls mean anything, and for BBO the block number is the real cost.

Machine: Apple M5 Pro (15 cores), macOS, Apple clang 21, `-O2`. The table uses these numbers. The same benchmark inside the Linux arm64 Docker VM is in [results/bench_linux.txt](results/bench_linux.txt). Its total times are close to the Mac's, but its per-op numbers are much higher (for example C's add is 126.2 ns there against 38.7 ns here). Since the totals match, the books themselves aren't slower there; the extra time comes from the per-op timer. I haven't pinned down exactly why it costs so much more inside the VM (a back-to-back `now()` pair only measures 32 ns there), so I only use the Mac numbers for per-op results.

[results/bench_macos.txt](results/bench_macos.txt):
```
Workload: 1000000 ops, seed 42 -> 700471 add, 199305 cancel, 100224 get_bbo, 10000 get_bids+get_asks(10)
steady_clock: resolution 41.0 ns, now()+now() pair 28.6 ns (included in every per-op number)

Implementation A (std::map + unordered_map)  (median of 5 runs)
  total time            :   145.26 ms
  ns / add_order        :    112.0
  ns / cancel_order     :    338.8
  ns / get_bbo          :     16.5   (block of 1M calls: 1.92 ns)
  ns / get_bids+asks(10):    147.0
  final BBO             : bid 99.99 x 2722 | ask 100.01 x 2515
  checksum              : fb65302f2675591e

Implementation B (heaps + lazy delete)  (median of 5 runs)
  total time            :   151.27 ms
  ns / add_order        :     41.4
  ns / cancel_order     :    495.8
  ns / get_bbo          :     15.3   (block of 1M calls: 2.91 ns)
  ns / get_bids+asks(10):   1108.3
  ...
Implementation C (tick array + bitmap + own hash)  (median of 5 runs)
  total time            :     14.59 ms
  ns / add_order        :     38.7
  ns / cancel_order     :     32.9
  ns / get_bbo          :     15.7   (block of 1M calls: 1.92 ns)
  ns / get_bids+asks(10):    106.6
  ...
```

These numbers move between runs. Across my runs A's total was anywhere from about 112 to 145 ms, so differences of 10 to 20% between A and B are within noise. The 10x gap to C is not.

## 6. Profiling

I only have a Mac, so I ran Linux perf in an Ubuntu 24.04 arm64 Docker container (kernel 6.12 linuxkit, perf 6.8). The macOS hypervisor doesn't pass hardware counters through to the VM, so `cycles`, `instructions`, `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and `dTLB-load-misses` all show `<not supported>`. Only task-clock and page-faults work there. To fill in the rest:
- IPC: from the Mac's own counters, read natively with `/usr/bin/time -l` ("instructions retired" and "cycles elapsed"). See [results/macos_time_l.txt](results/macos_time_l.txt).
- Branch and cache misses: from `valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes`. These are simulated, but the instruction counts are exact. I set the simulated caches to what earlier Apple M-series P-cores use (128 KB 8-way L1D, 16 MB last level). Apple doesn't publish the M5's cache sizes, so this is an assumption. See [results/cachegrind.txt](results/cachegrind.txt).
- dTLB misses: I had no way to get these.

Every counter run uses `--runs 1 --total-only`, so it's exactly one pass over the workload with no per-op timers adding noise. I also ran each tool on `--impl none` (only generates the workload) and subtracted that, so the "net" numbers below are just the order book.

### perf stat ([results/perf_stat.txt](results/perf_stat.txt))
```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X --runs 1 --total-only

impl none : 24.02 msec task-clock (+-14.6%)   802 page-faults    (other 7 counters: <not supported>)
impl A    : 164.46 msec task-clock (+-4.1%)  7211 page-faults    (other 7 counters: <not supported>)
impl B    : 239.54 msec task-clock (+-12.0%) 9024 page-faults    (other 7 counters: <not supported>)
impl C    : 43.93 msec task-clock (+-6.3%)    935 page-faults    (other 7 counters: <not supported>)
```

### perf record ([A](results/perf_report_A.txt), [B](results/perf_report_B.txt), [C](results/perf_report_C.txt))
Sampled on `cpu-clock` at 20 kHz with frame pointers (`make bench_prof`), shown as flat self-time. Most of the container code is inlined into the `Book` functions.

| A | B | C |
|---|---|---|
| 33.0% `BookA::add_order` (map + hash insert, inlined) | 50.0% `BookB::cancel_order` | 32.5% `BookC::add_order` |
| 28.2% `BookA::cancel_order` | 10.5% `BookB::add_order` | 16.3% kernel `clear_page` (zeroing new pages) |
| 7.5% `unordered_map::_M_rehash` | 4.5% + 4.5% + 3.6% red-black tree insert/erase/rebalance | 16.2% `run_once` (the loop, with `get_bbo` inlined) |
| about 20% `malloc`/`free`/`malloc_consolidate` | about 11% `malloc`/`free` | 10.5% + 6% workload generation and mt19937 |
| 2.1% workload generation | 2.7% `memcpy` (heap copies in `get_bids`) | 5.6% `BookC::cancel_order`, 2.9% `llround` |

## 7. Results table (brief section 10)

"Net" means the `--impl none` baseline is subtracted. "Per op" divides by the 1,010,000 calls in the workload.

| Metric | A (map + hash) | B (heaps) | C (array + bitmap + own hash) |
|---|---|---|---|
| Total time (ms), macOS median of 5 | 145.3 | 151.3 | 14.6 |
| ns / add (per-op timer) | 112.0 | 41.4 | 38.7 |
| ns / cancel (per-op timer) | 338.8 | 495.8 | 32.9 |
| ns / BBO, per-op timer (block of 1M) | 16.5 (1.92) | 15.3 (2.91) | 15.7 (1.92) |
| ns / get_bids+get_asks(10) | 147.0 | 1108.3 | 106.6 |
| Instructions, net (cachegrind, exact) | 502.8 M (498 per op) | 710.1 M (703 per op) | 119.8 M (119 per op) |
| IPC, whole process (Mac hardware counters) | 1.28 | 1.44 | 1.76 |
| IPC, net of baseline (Mac hardware counters) | 1.09 | 1.29 | 1.09 |
| Branch misses, net (cachegrind, simulated) | 8.90 M of 87.3 M = 10.2% | 6.44 M of 133.4 M = 4.8% | 0.80 M of 15.8 M = 5.1% |
| L1-dcache-load misses, net (cachegrind, simulated) | 10.19 M (10.1 per op) | 9.83 M (9.7 per op) | 2.17 M (2.2 per op) |
| Cache misses (last-level D misses, net, cachegrind) | 2.22 M | 1.87 M | 1.40 M |
| dTLB-load-misses | not supported | not supported | not supported |
| Page faults, net (Linux perf stat) | 6,409 | 8,222 | 133 |
| Minor page faults, net (macOS `time -l`) | 2,078 | 2,045 | 1,037 |
| Linux task-clock, net (ms) | 140.4 | 215.5 | 19.9 |

## 8. Performance analysis (brief section 11)

### 11.1 Which is fastest?
C is the fastest at every operation. Its total time is about 10 times lower than A and B (14.6 ms against 145 to 151 ms), roughly 14 ns per op over the whole workload.

- add: C (38.7 ns) and B (41.4 ns) are close, and both numbers are mostly the timer (about 29 ns per pair, 41 ns resolution). A is clearly slower at 112 ns. Every add allocates a hash node and sometimes a tree node, and the `unordered_map` rehashes as it grows (7.5% of A's samples).
- cancel: this is where they differ most. C takes 33 ns, A 339 ns, B 496 ns. Cancels hit random old orders, so their data is usually not in cache.
  - C: one hash probe into a flat array, one array decrement, and sometimes a bitmap scan.
  - A: a walk along a hash bucket chain, then a tree search of about 12 levels for the price, then two `free`s.
  - B: a red-black tree search of about 19 levels over roughly 500k order nodes, rebalancing, a hash lookup for the level, then heap pops if a level emptied.
- get_bbo: O(1) in all three. Timed as a block of 1M calls, A and C take 1.9 ns and B 2.9 ns; the 15 ns per-call figure is the clock. A reads `begin()` of the map, which the map keeps cached. C reads two cached ints and two array cells. B's `top()` is cheap too, but then it needs a hash lookup for the level's quantity.
- depth: C (107 ns for both sides) walks the bitmap with about one `ctz` per level. A (147 ns) walks 10 tree nodes. B (1.1 microseconds) has to copy its whole heap, thousands of entries including dead ticks, and then pop 10 times.

Most of C's advantage is doing less work: cachegrind counts 119 instructions per op for C, against about 500 for A and 700 for B. C also touches far fewer cache lines, 2.2 L1 misses per op against about 10.

### 11.2 Branch prediction
A has the most branch misses: 10.2% of its branches, 8.9 M in total, about 9 per op. Most come from the tree searches. Prices are uniform over 5000 ticks, so at each node of the red-black tree going left or right is close to a coin flip, and a 12-level search costs several mispredictions. The end of the hash bucket chain walk adds another one. B's rate is lower (4.8%) only because it also runs a lot of easy, predictable branches in the heap and loop code. It still mispredicts 6.4 M times, for the same reason: its 19-level order tree is searched with random ids. C has 0.8 M mispredicts, under one per op. It has very few data-dependent branches: an array index replaces the price search, the hash probe usually hits the first slot at a load factor of 0.7 or less, and `clz`/`ctz` replace the loops that search for the next level.

### 11.3 Cache behaviour
A and B have the most cache misses, about 10 L1D misses per op against 2.2 for C.

A and B follow pointers and allocate many small objects. There is a tree or hash node for every level and every order (about 48 to 64 bytes each), so hundreds of thousands of separate `malloc` blocks end up spread over the heap. Each step of a tree search is a dependent load from an address the CPU can't predict, so it can't prefetch, and each step can miss. `malloc`/`free` alone are about 20% of A's profile and 11% of B's.

C mostly accesses contiguous memory. The level array is 120 KB and the bitmap 2 KB, and they stay hot in L1/L2 (if the M5's L1D is 128 KB like earlier M-series chips, most of it fits in L1). The cold access is the order's hash slot, roughly one miss per add or cancel, and that is now C's main cost. Fibonacci hashing spreads ids across the 16 MB table on purpose, so even consecutive adds write to random cache lines. That shows up in C's last-level misses: 1.4 M, lower than A and B but not by much.

IPC agrees with this. Whole-process IPC is highest for C (1.76), but that's because most of C's run is the workload generator, which runs at high IPC. With the generator subtracted, C's IPC is about 1.1, the same as A. C runs 4 to 6 times fewer instructions, but the remaining ones are slowed down by the hash-slot misses.

### 11.4 Page faults
B has the most page faults (8,222 net on Linux), then A (6,409). Both grow by allocating. B's `std::map` allocates a node per order, its hashed levels and two heaps grow by reallocating, and `get_bids` makes a new copy of the heap on every call. A allocates a node per order and per level, and rehashes its order table several times as it grows. Each new page `malloc` gets from the kernel is a page fault.

C has only 133 net faults. It allocates its 16 MB table and 120 KB array once, in the constructor. On Linux I think the large allocation is backed by transparent huge pages, which would explain why 16 MB costs so few faults. The kernel zeroes those pages up front, which is the 16% `clear_page` in C's profile; in a real system that would happen at start-up, not while trading. On macOS (16 KB pages, no huge pages here) C has 1,037 faults, about 16 MB / 16 KB, which is the table being touched once. So C's memory is a fixed, up-front allocation, while A and B keep allocating as the number of orders grows.

### 11.5 Production choice
I would deploy C. Reasons beyond the average latency:
- Latency: fastest on every operation, and cancel is 10 to 15 times faster than A or B.
- Predictability: no allocation after start-up, so no `malloc` stalls, and every operation is O(1) with small constants. A and B depend on tree depth and allocator state, and B's `get_bids` gets slower as dead ticks build up in the heap.
- Memory: fixed and known, 16 MB of order slots for 1M orders plus 120 KB of levels. A and B use a bit less at this size, but they keep growing and their nodes are scattered.
- Cache: all level data is hot and contiguous, with about one cold miss per order operation.
- Complexity and maintainability: C is about 250 lines of custom code (bitmap scans, backward-shift deletion) against about 80 lines of standard containers for A, and it hard-codes the tick range, so a new price band means resizing. I'm fine with that because of the `--verify` test: A is simple enough to trust, and C is checked against it on every op.

### 11.6 What I would try next
- Index orders directly by id. Exchange ids are usually sequential, so a slab `orders[id - base]` with a hash only as fallback would turn C's random hash miss into a mostly sequential write.
- Smaller order slots: a 16-bit tick offset and 32-bit quantity with a 64-bit id gives 12-byte slots (8 with 32-bit ids), so more orders per cache line.
- Pre-fault the table at start-up (`MAP_POPULATE` or touching every page) and use huge pages explicitly, to keep page faults and TLB misses off the hot path.
- An integer-tick API, since `llround` alone is 2.9% of C's profile.
- Measure tail latency (p99, p99.9) with a cycle counter, because `steady_clock` is too coarse at 41 ns on this machine. A's and B's allocations would probably show up more in the tail than in the average.
- Fix B's depth query by keeping a small sorted cache of the top levels instead of copying the heap.
- Add a per-level FIFO (an intrusive list in the order slab) if this ever needs matching.
