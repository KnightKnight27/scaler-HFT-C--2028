# Capstone - Low Latency Order Book

## email: adam.24bcs10017@sst.scaler.com
## roll_no: 24bcs10017

Order book for one instrument, made 3 different ways (A, B, C), with one benchmark program that
can run any of them plus a `--verify` mode that checks all 3 give the same answers.

## Files

| file | what |
|---|---|
| `common.h` | `Side`, `Level`, `BBO`, tick helpers, and `OrderBookBase` (the common interface) |
| `bookA.h` | Implementation A - `std::map` price levels + `std::list` per level + `unordered_map` for ids |
| `bookB.h` | Implementation B - sorted `std::vector` of levels + `std::map` for ids |
| `bookC.h` | Implementation C (own design) - array indexed by price tick + cached best + vector indexed by id |
| `bench.cpp` | benchmark + correctness test (one program) |
| `run_perf.sh` | script to build + run verify + perf stat + perf record for A, B, C on linux |

## How to build

```
g++ -std=c++17 -O2 -o bench bench.cpp
```

(add `-g` if you want function names in `perf report`, `run_perf.sh` does that already)

## How to run the correctness test

```
./bench --verify
```

It does 2 things:
1. small hand written tests on each book with known answers: empty book BBO, cancel unknown id -> false,
   cancel same id twice -> 2nd is false, 2 orders on the same price get summed, `get_bids(10)` when only 3 levels exist gives 3, etc.
2. runs the same 1,000,000 op random workload (seed 42) through A, B and C at the same time, and after
   **every** op compares the BBO, the cancel return value and the top 10 bid/ask levels. Every 10,000 ops it compares the
   **whole** book (all levels, prices and qtys). If the 3 dont agree it prints which one is the odd one out.

I also checked that the test actually catches bugs by breaking `get_asks` in C on purpose (skipping levels with qty 1):
it printed `Implementation C: FAIL` and A/B PASS.

## How to run the benchmark

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Optional: `--runs N` (default 5) and `--ops N` (default 1,000,000).

Each run does 5 runs on a fresh book and prints the median. On linux run `./run_perf.sh` to get everything incl. perf.

### Workload
- 1,000,000 ops made up front with `std::mt19937 rng(42)` so it is the exact same for A, B and C (and generating it isnt timed)
- ~70% add, ~20% cancel, ~10% get_bbo (actual: add 700829, cancel 199496, bbo 99675)
- every 100 ops: `get_bids(10)` and `get_asks(10)`
- side 50/50, qty 1-100, price = random valid tick in the bid band [50.00, 99.99] or ask band [100.01, 150.00]
- cancels always pick a random order that is still live (kept in a vector of live ids)
- order ids are 1, 2, 3, ...

### Timing
- `std::chrono::steady_clock` around every single add / cancel / get_bbo call, added up per type then divided by the count
- total time = whole loop (it includes the clock calls, so it is a bit bigger than just the sum of the ops)
- final BBO + a checksum (built from cancel results, BBO results and depth results) is printed so the compiler cant remove the work

## Interface changes

The API is the same as in the assignment (`double` prices, `long` qty, same 5 functions, same `Side`/`Level`/`BBO`).
Only changes:
- all 3 books inherit from an abstract class `OrderBookBase` (virtual functions) so the benchmark can use any of them through one pointer.
  The classes are called `OrderBookA`, `OrderBookB`, `OrderBookC`.
- inside, prices are converted to integer ticks (`price * 100`, rounded) and converted back to double when returned.
- `add_order` ignores bad input instead of crashing: price outside the band for that side, qty <= 0, or an id that is already live.
  All 3 books do the same thing here so they still agree.

## Data structure choices

P = number of price levels on one side (max 5000), N = number of live orders, d = depth asked for.

### Implementation A - std::map + list + unordered_map

- **Orders**: each order (id, qty) is a node in a `std::list` that belongs to its price level.
- **Price levels**: `std::map<int, LevelA>` (red black tree), bids with `greater<int>` so the highest price is first, asks normal so the lowest is first. `LevelA` has the total qty + the list.
- **Best bid/ask**: `bids.begin()` / `asks.begin()`.
- **Cancel lookup**: `unordered_map<id, {side, price, list iterator}>`. Find the id, `map::find` the price, erase the list node with the iterator, remove the level if it is empty.

| Operation | Complexity |
|---|---|
| add_order | O(log P) + O(1) avg hash |
| cancel_order | O(1) avg hash + O(log P) map find |
| get_bbo | O(1) |
| get_bids | O(d) |
| get_asks | O(d) |

### Implementation B - sorted vector + std::map of orders

- **Orders**: `std::map<uint64_t, OrderB>` keyed by id (side, price, qty). No per level list, because only the total per price is needed for the API.
- **Price levels**: `std::vector<LevelB>` of (price, qty), kept sorted. Bids go low->high and asks go high->low so that **the best price is always at the back** (inserting/removing near the best only moves a few elements).
- **Best bid/ask**: `back()` of each vector.
- **Cancel lookup**: `std::map::find(id)` (tree, O(log N)), then binary search the level in the vector and subtract, erase the level if it hits 0.

| Operation | Complexity |
|---|---|
| add_order | O(log N) + O(log P) binary search, + O(P) shift when a new level is created |
| cancel_order | O(log N) + O(log P), + O(P) shift when a level gets removed |
| get_bbo | O(1) |
| get_bids | O(d) |
| get_asks | O(d) |

### Implementation C - price indexed array (own design)

- **Orders**: `std::vector<OrderC>` indexed directly by the order id (`byId[id]`), each slot has alive flag, side, price, qty. Grows by doubling. Ids >= 50,000,000 go into an `unordered_map` instead so a huge id doesnt allocate gigabytes.
- **Price levels**: two plain arrays `bidQty[tick]` and `askQty[tick]` (15001 longs each). qty 0 = no level.
- **Best bid/ask**: kept in two ints `bestBid` / `bestAsk`. Add just compares with it. If a cancel empties the best level, walk the array (down for bids, up for asks) to the next non zero level.
- **Cancel lookup**: `byId[id]` - one array access, no hashing.

| Operation | Complexity |
|---|---|
| add_order | O(1) (amortized, vector growth) |
| cancel_order | O(1), worst case O(P) when the best level is emptied and we have to scan |
| get_bbo | O(1) |
| get_bids | O(d) when the book is dense, worst O(P) if levels are spread out |
| get_asks | O(d) when the book is dense, worst O(P) |

The 3 designs are different in every part the assignment asks about:

| | A | B | C |
|---|---|---|---|
| orders stored in | `std::list` nodes per level | `std::map` nodes keyed by id | `std::vector` slot = id |
| levels stored in | `std::map` (tree) | sorted `std::vector` | fixed array indexed by tick |
| best price | `map.begin()` | `vector.back()` | cached int + scan |
| cancel lookup | hash map -> list iterator | tree search by id | direct index |

## Correctness results

```
$ ./bench --verify
Running correctness test...

Basic tests (empty book, unknown cancel, double cancel, short depth)...
Random workload: 1000000 ops, seed 42, comparing after every op...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.

Final BBO:
Bid: 99.99 x 3244
Ask: 100.01 x 2193
```

All 3 benchmark runs also print the same final BBO and the same checksum `2516203052`.

## Performance results

Machine for the timing numbers: Intel Core Ultra 5 226V, Windows 11, MinGW g++ 6.3.0, `-O2`.

> NOTE: perf only works on linux. The perf rows below are still TODO - run `./run_perf.sh` on the lab linux machine
> and paste the numbers + outputs here. The timing rows are from my laptop (Windows has a coarser clock, so the
> ns/BBO numbers are pretty noisy, the calls are so short that the clock resolution is most of what you see).

### Benchmark output (median of 5 runs)

```
Implementation A (std::map levels + list per level + unordered_map ids)
run 1: total 350.66 ms | add 303.58 ns | cancel 436.14 ns | bbo 19.92 ns
run 2: total 352.17 ms | add 281.51 ns | cancel 486.30 ns | bbo 33.96 ns
run 3: total 352.74 ms | add 310.28 ns | cancel 446.44 ns | bbo 30.11 ns
run 4: total 342.64 ms | add 259.26 ns | cancel 490.18 ns | bbo 90.51 ns
run 5: total 379.85 ms | add 296.93 ns | cancel 520.72 ns | bbo 20.06 ns
Median: total 352.17 ms | add 296.93 ns | cancel 486.30 ns | bbo 30.11 ns

Implementation B (sorted vector levels + std::map ids)
run 1: total 402.29 ms | add 323.52 ns | cancel 688.24 ns | bbo 10.04 ns
run 2: total 391.29 ms | add 289.66 ns | cancel 615.90 ns | bbo 63.85 ns
run 3: total 424.93 ms | add 292.80 ns | cancel 683.86 ns | bbo 39.84 ns
run 4: total 394.24 ms | add 292.05 ns | cancel 704.65 ns | bbo 40.95 ns
run 5: total 397.57 ms | add 284.74 ns | cancel 677.89 ns | bbo 60.51 ns
Median: total 397.57 ms | add 292.05 ns | cancel 683.86 ns | bbo 40.95 ns

Implementation C (price indexed array + cached best + vector by id)
run 1: total 104.06 ms | add 62.24 ns | cancel 84.60 ns | bbo 10.18 ns
run 2: total 98.19 ms | add 49.96 ns | cancel 70.67 ns | bbo 19.93 ns
run 3: total 101.78 ms | add 68.59 ns | cancel 59.61 ns | bbo 71.16 ns
run 4: total 108.04 ms | add 64.10 ns | cancel 85.23 ns | bbo 39.88 ns
run 5: total 97.45 ms | add 51.49 ns | cancel 68.28 ns | bbo 20.04 ns
Median: total 101.78 ms | add 62.24 ns | cancel 70.67 ns | bbo 20.04 ns

Final BBO (same for all 3):
Bid: 99.99 x 3244
Ask: 100.01 x 2193
Checksum: 2516203052
```

### Results table

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 352.17 | 397.57 | 101.78 |
| ns / add | 296.93 | 292.05 | 62.24 |
| ns / cancel | 486.30 | 683.86 | 70.67 |
| ns / BBO | 30.11 | 40.95 | 20.04 |
| IPC | TODO | TODO | TODO |
| Branch misses (%) | TODO | TODO | TODO |
| L1-dcache-load-misses | TODO | TODO | TODO |
| Cache misses | TODO | TODO | TODO |
| Page faults | TODO | TODO | TODO |

### perf stat - Implementation A

Running correctness test...

Basic tests (empty book, unknown cancel, double cancel, short depth)...
Random workload: 1000000 ops, seed 42, comparing after every op...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.

Final BBO:
Bid: 99.99 x 3244
Ask: 100.01 x 2193
===== Implementation A =====
Implementation A (std::map levels + list per level + unordered_map ids)
ops: 1000000  (add 700829, cancel 199496, bbo 99675, + get_bids/get_asks every 100 ops)
runs: 5

run 1: total 237.54 ms | add 205.83 ns | cancel 362.20 ns | bbo 19.18 ns
run 2: total 234.39 ms | add 193.93 ns | cancel 382.61 ns | bbo 20.10 ns
run 3: total 198.00 ms | add 164.21 ns | cancel 319.22 ns | bbo 17.18 ns
run 4: total 218.52 ms | add 180.01 ns | cancel 357.64 ns | bbo 19.32 ns
run 5: total 220.36 ms | add 182.39 ns | cancel 359.17 ns | bbo 19.08 ns

Median of 5 runs:
Total time (ms): 220.36
ns / add:        182.39
ns / cancel:     359.17
ns / BBO:        19.18

Final BBO:
Bid: 99.99 x 3244
Ask: 100.01 x 2193

Checksum: 2516203052
./run_perf.sh: line 17: perf: command not found
./run_perf.sh: line 23: perf: command not found
===== Implementation B =====
Implementation B (sorted vector levels + std::map ids)
ops: 1000000  (add 700829, cancel 199496, bbo 99675, + get_bids/get_asks every 100 ops)
runs: 5

run 1: total 233.31 ms | add 162.62 ns | cancel 495.99 ns | bbo 17.68 ns
run 2: total 237.46 ms | add 157.28 ns | cancel 531.51 ns | bbo 18.85 ns
run 3: total 232.73 ms | add 156.19 ns | cancel 511.93 ns | bbo 21.24 ns
run 4: total 228.91 ms | add 155.05 ns | cancel 499.10 ns | bbo 18.83 ns
run 5: total 219.23 ms | add 150.00 ns | cancel 471.05 ns | bbo 17.98 ns

Median of 5 runs:
Total time (ms): 232.73
ns / add:        156.19
ns / cancel:     499.10
ns / BBO:        18.83

Final BBO:
Bid: 99.99 x 3244
Ask: 100.01 x 2193

Checksum: 2516203052
./run_perf.sh: line 17: perf: command not found
./run_perf.sh: line 23: perf: command not found
===== Implementation C =====
Implementation C (price indexed array + cached best + vector by id)
ops: 1000000  (add 700829, cancel 199496, bbo 99675, + get_bids/get_asks every 100 ops)
runs: 5

run 1: total 57.65 ms | add 32.23 ns | cancel 71.67 ns | bbo 19.26 ns
run 2: total 55.28 ms | add 28.86 ns | cancel 73.51 ns | bbo 18.43 ns
run 3: total 52.64 ms | add 29.73 ns | cancel 63.89 ns | bbo 17.80 ns
run 4: total 53.92 ms | add 29.51 ns | cancel 66.75 ns | bbo 18.94 ns
run 5: total 54.22 ms | add 30.25 ns | cancel 65.48 ns | bbo 18.41 ns

Median of 5 runs:
Total time (ms): 54.22
ns / add:        29.73
ns / cancel:     66.75
ns / BBO:        18.43

Final BBO:
Bid: 99.99 x 3244
Ask: 100.01 x 2193

Checksum: 2516203052
./run_perf.sh: line 17: perf: command not found
./run_perf.sh: line 23: perf: command not found
done, look in perf_results/

## Performance analysis

### 11.1 Which implementation is fastest?

**C is the fastest** for add (~62 ns vs ~295 ns) and cancel (~71 ns vs 486 / 684 ns), and about 3.5x faster in total.
For get_bbo all 3 are O(1) and really close (20-40 ns, which is mostly the cost of calling `steady_clock::now()` twice
and the virtual call), so I wouldnt say one really wins there, C is just the cheapest because it is 2 array reads.

Why C is faster:
- add in C is: index `byId[id]`, write 4 fields, `bidQty[tick] += qty`, compare with bestBid. No allocation, no hashing, no tree walk.
  A has to hash the id, insert into the `unordered_map` (allocates a node), walk a tree of ~5000 levels (~13 levels deep), and allocate a list node.
  B has to walk a `std::map` with up to ~500k orders (~19 levels deep) and allocate a tree node, plus a binary search in the level vector.
- cancel in C is again one direct index + one array write. The "scan for the next best" almost never happens because
  with 1M ops the book has thousands of orders at every price, so the best level hardly ever gets empty.
  B is the slowest at cancel because `std::map::find` over ~500k random ids is a pointer chase through nodes spread all over the heap.

Where the time goes (perf record): TODO - expected: A in `std::_Hashtable` / `_Rb_tree` / `operator new`, B in `_Rb_tree` (find/insert/erase on the orders map), C mostly in `runOnce` itself and `steady_clock::now`.

### 11.2 Branch prediction

TODO check with perf: I expect **B** to have the most branch misses (or A, they are close).

Reasons:
- the binary search in B (`findPos`) does `if (before) lo = mid+1 else hi = mid` with random prices, so that branch is basically a coin flip each step (~13 steps per call).
- `std::map` lookups (A and B) do a compare at every tree node going left or right, also random because the prices/ids are random.
- A also has branches inside the hash table (bucket chains).
- C has almost no data dependent branches, `if (o->alive)`, `if (t > bestBid)` and `bidQty[p]==0` are almost always the same way, so the predictor gets them right.
- The op type branch in the benchmark loop (add/cancel/bbo random 70/20/10) is the same for all 3 so it doesnt explain differences.

### 11.3 Cache behavior

TODO check with perf: I expect **B or A** to have the most cache misses and C the least.

- **A**: follows pointers everywhere. `unordered_map` node -> list node, plus a tree walk for the price. Every order is its own small heap allocation (hash node + list node), so ~1.4M small objects are created over a run and they are spread around memory.
- **B**: the level vector is contiguous and small (5000 x 16 bytes = 80KB, fits in L2) so the price part is cache friendly, but the `std::map` of orders is a big tree (~500k nodes x ~64 bytes = ~30MB) and a lookup touches ~19 nodes that are in random places -> a cache miss at almost every step. This is why B cancel is the slowest.
- **C**: price arrays are contiguous and 2 x 120KB (fit in L2), `byId` is one contiguous array. Each add/cancel touches about 2 cache lines. No pointers to follow, no small allocations. `get_bids` walks neighbouring array slots so the prefetcher helps.

Cache misses hurt because a miss to RAM is ~100 ns, and in A/B those misses are dependent (you need the node to know where the next node is), so the CPU cant do them in parallel.

### 11.4 Page faults

TODO check with perf: I expect **A** (or B) to have the most page faults.

A and B allocate a new heap node for every order (A even 2 per order), so over 700k adds the heap keeps growing and touching new pages.
C grows `byId` by doubling, so it only allocates a few times, but each resize copies the whole vector into fresh memory,
so its page faults should be lower but not zero (~700k x 24 bytes = ~17MB ends up being touched).
Note the workload vector (1M ops x 40 bytes) is made the same way in all 3 so it adds the same faults to each.

### 11.5 Production choice

I would pick **C** (with some changes).

- **Latency**: lowest for add and cancel, and get_bbo is just 2 reads.
- **Predictability**: no `malloc` in the hot path (except when `byId` grows, which could be fixed by reserving up front), no tree rebalancing, no hash rehash. A and B both can have random slow ops when `unordered_map` rehashes (A) or a new level makes the vector shift (B).
  The one bad case in C is the scan when the best level empties and the book is very sparse (up to 5000 slots), but 5000 longs is a fast linear scan of contiguous memory.
- **Memory**: price arrays are fixed (~240KB). The weak point is `byId`: memory depends on the biggest id, not on how many orders are live, and it never shrinks. In a real system I would use an id->slot hash map with a pool of order slots instead.
- **Cache**: best of the 3 (see 11.3).
- **Complexity / maintainability**: the code is short but has more "tricks" (the 0 = empty convention, the scan, the BIG_ID fallback). A is the easiest to read and to trust, so A would be my backup choice if the price range wasnt fixed. C only works because the price band is small and known (10,000 ticks); if the instrument could trade at any price A is safer.

### 11.6 What would I try next

- in C: replace the vector-by-id with an open addressing hash map (id -> slot index) + a preallocated pool of order slots, so memory doesnt depend on id values
- `reserve()` everything at startup (orders pool, hash table) so there are no allocations during trading
- a bitmap of non empty levels (5000 bits = 79 x 64 bit words) for C so finding the next best level is a few `__builtin_ctzll` calls instead of a scan
- in A: store the map iterator in the order info so cancel doesnt need `map::find`
- in A/B: use a pool allocator for the nodes so they are close together in memory
- get rid of the virtual calls (templates instead of `OrderBookBase`) so the compiler can inline everything
- use integer ticks in the public API too so there is no double conversion on every call
- use rdtsc instead of `steady_clock` for the per op timing because the clock call itself is a big part of a 20 ns BBO
