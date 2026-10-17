# Capstone: Low Latency Order Book

Team of 2

| Name | Roll number | Email |
|------|-------------|-------|
| Himanshu Chauhan | 24BCS10071 | himanshu.24bcs10071@sst.scaler.com |
| Harshit Sen | 24BCS10003 | harshit.24bcs10003@sst.scaler.com |

This is a resting order book for one instrument. Orders get added and cancelled, nothing gets matched. We wrote it three times with different data structures and then benchmarked and profiled all three.

The analysis and the results table are in [REPORT.md](REPORT.md).

## Files

```
common.h       Side, Level, BBO, Order, price <-> tick helpers
book_a.h       Implementation A (std::map + unordered_map)
book_b.h       Implementation B (sorted vector + std::map)
book_c.h       Implementation C (price indexed array + vector indexed by id)
bench.cpp      benchmark and --verify, one program for all three
run_perf.sh    builds, runs verify, bench, perf stat and perf record for A, B, C
results/       output we got from our machine
```

## Build

Only g++ is needed. From inside the `Capstone` folder:

```
g++ -O2 -std=c++17 -o bench bench.cpp
```

For perf we also add `-g -fno-omit-frame-pointer` so the call graphs have names in them. `run_perf.sh` does this already.

## Correctness test

```
./bench --verify
```

First it runs a few hand written cases on each book: empty book, cancelling an id that doesn't exist, cancelling the same order twice, asking for 10 levels when there are only 3, and out of band prices. After that it builds the same 1,000,000 op workload as the benchmark and feeds it to A, B and C at the same time. After every single op it compares the BBO and the top 10 bid and ask levels (price and qty). Every 10,000 ops it compares the whole book on both sides. Every cancel is also done a second time to check that all three return false.

A is used as the reference. If B or C differ, it prints the op number where they first went wrong.

Our output:

```
Running correctness test...

edge cases  A: ok  B: ok  C: ok
workload: 1000000 ops, BBO + top 10 levels checked after every op, full book every 10000 ops
final levels  bids: 5000  asks: 5000

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## Benchmark

```
./bench --impl A
./bench --impl B
./bench --impl C
```

By default it does 5 runs and prints each run and the median. `--runs N` changes the number of runs. We use `--runs 1` under `perf stat -r 5` so that perf does the repeating.

The workload is made once at the start with `std::mt19937 rng(42)`, so every implementation gets exactly the same list of ops:

- 1,000,000 ops, roughly 70% add, 20% cancel, 10% get_bbo
- side is 50/50, qty is 1 to 100
- price is a random tick in the right band (bids 50.00 to 99.99, asks 100.01 to 150.00)
- a cancel always picks a random order that is still live
- every 100 ops we also call get_bids(10) and get_asks(10)

Each call is timed on its own with `std::chrono::steady_clock`. At the end it prints the final BBO and a checksum. The checksum adds up the cancel results, the BBO quantities and the depth quantities, so the compiler can't throw the work away. All three give the same checksum, which is one more sign they agree:

```
Final BBO:
Bid: 99.99 x 2722
Ask: 100.01 x 2525

Checksum: 491457988
```

## perf

```
bash run_perf.sh
```

This runs `perf stat -r 5` with task-clock, cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses, dTLB-load-misses and page-faults for each implementation, then `perf record -g` and a flat `perf report`. Text output goes in `results/`. The `.data` files are written to `/tmp` because perf record failed when writing to our OneDrive folder through WSL. To look at the full call graph:

```
perf report -i /tmp/perf_A.data
```

We ran everything in WSL2 (Ubuntu 26.04, g++ 15.2, perf 7.0) on an i5-13420H laptop. WSL2 doesn't pass the hardware counters through, so only task-clock and page-faults work. The others show `<not supported>`. See the note at the bottom.

## Interface changes

The API is the one given in the assignment. `add_order` still takes a `double` price and `get_bbo`, `get_bids`, `get_asks` still return doubles. Inside, every book turns the price into an integer tick right away (`llround(price * 100)`, so 100.01 becomes 10001) and only stores ints. When reading out we divide by 100 again. This way two books never disagree because of floating point.

Some things we decided ourselves because the assignment didn't say:

- `add_order` with a price outside its band, a qty of 0 or less, or an id that is already live is ignored.
- `get_bids` and `get_asks` return the best level first.
- Each book is its own class (`BookA`, `BookB`, `BookC`) with the same five methods. There is no base class. The benchmark is a template so the same code runs all three.
- Book C uses the order id as a vector index. This is fine for ids like ours that start at 1 and go up, but a random 64 bit id would make it allocate a huge vector.

## The three designs

### A: std::map for levels, unordered_map for orders

- Orders: `std::unordered_map<uint64_t, Order>`, id to {side, tick, qty}.
- Levels: two `std::map<int, long>`, tick to total qty. Bids use `std::greater<int>` so the highest price comes first.
- Best bid / ask: `bids.begin()` and `asks.begin()`.
- Cancel: find the order in the hash map, take its qty off its level and erase the level if it hits 0.

### B: sorted vector for levels, std::map for orders

- Orders: `std::map<uint64_t, Order>`. This is a red-black tree, so lookup is by comparing ids, not hashing.
- Levels: two `std::vector<PriceLevel>` kept sorted. The best price is always at the back, so bids are sorted low to high and asks high to low.
- Best bid / ask: `bids.back()` and `asks.back()`.
- Adding to a level: our own binary search finds the spot. If the tick is already there we add to it, if not we `insert` into the vector, which shifts everything after it.
- Cancel: tree lookup for the order, binary search for the level, `erase` from the vector if it hits 0.

### C: array indexed by price, vector indexed by order id

- Orders: `std::vector<Order>` where `orders[id]` is the order. A cancelled order just gets qty 0.
- Levels: one `std::vector<long>` with 15,001 slots, `levels[tick]` is the total qty at that tick. It is allocated once in the constructor and never resized.
- Best bid / ask: kept in two ints, `best_bid` and `best_ask`. Add moves them if the new price is better. When a cancel empties the best level we walk the array down (bids) or up (asks) until we find a non-empty tick.
- Cancel: `orders[id]` directly, no search at all.
- Depth: start at the best tick and walk the array, skipping empty ticks.

### Complexity

P = number of price levels on that side (at most 5000), N = number of live orders, d = depth asked for, R = size of the price band (5000 ticks).

| Operation | A | B | C |
|-----------|---|---|---|
| add_order | O(log P) + O(1) avg hash | O(log N) + O(log P), plus O(P) when a new level is inserted | O(1) amortised |
| cancel_order | O(1) avg hash + O(log P) | O(log N) + O(log P), plus O(P) when a level is erased | O(1), O(R) worst case when the best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(d) | O(d) | O(R) worst case, about O(d) when the book is dense |
| get_asks | O(d) | O(d) | O(R) worst case, about O(d) when the book is dense |

## perf stat output

Implementation A

```
 Performance counter stats for './bench --impl A --runs 1' (5 runs):

            369.32 msec task-clock:u                                                            ( +-  7.78% )
   <not supported>      cycles:u
   <not supported>      instructions:u
   <not supported>      branches:u
   <not supported>      branch-misses:u
   <not supported>      cache-misses:u
   <not supported>      L1-dcache-load-misses:u
   <not supported>      dTLB-load-misses:u
             19305      page-faults:u                                                           ( +-  0.00% )

       0.377229092 +- 0.028518687 seconds time elapsed  ( +-  7.56% )
```

Implementation B

```
 Performance counter stats for './bench --impl B --runs 1' (5 runs):

            463.04 msec task-clock:u                                                            ( +-  2.72% )
   <not supported>      cycles:u
   <not supported>      instructions:u
   <not supported>      branches:u
   <not supported>      branch-misses:u
   <not supported>      cache-misses:u
   <not supported>      L1-dcache-load-misses:u
   <not supported>      dTLB-load-misses:u
             21713      page-faults:u                                                           ( +-  0.00% )

       0.471359472 +- 0.012145401 seconds time elapsed  ( +-  2.58% )
```

Implementation C

```
 Performance counter stats for './bench --impl C --runs 1' (5 runs):

            139.79 msec task-clock:u                                                            ( +-  5.19% )
   <not supported>      cycles:u
   <not supported>      instructions:u
   <not supported>      branches:u
   <not supported>      branch-misses:u
   <not supported>      cache-misses:u
   <not supported>      L1-dcache-load-misses:u
   <not supported>      dTLB-load-misses:u
             22164      page-faults:u                                                           ( +-  0.00% )

       0.150542047 +- 0.009048707 seconds time elapsed  ( +-  6.01% )
```

Note: these counts cover the whole program, so they include building the workload too. That part is identical for all three.

Unavailable counters: cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses and dTLB-load-misses are all `<not supported>` for us. WSL2 runs inside Hyper-V, which doesn't pass the CPU's hardware counters to the Linux VM. The WSL kernel says so itself at boot:

```
Performance Events: unsupported CPU family 6 model 186 no PMU driver, software events only.
```

`perf list hw` prints nothing (see `results/pmu_check.txt`). So we report task-clock and page-faults, which did work, and the perf record profiles. On a normal Linux install `run_perf.sh` collects all of the counters without any changes.
