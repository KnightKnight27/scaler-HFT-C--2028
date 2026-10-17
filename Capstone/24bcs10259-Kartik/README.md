# Capstone - Low Latency Order Book

- Name: Kartik
- Email: kartik.24bcs10259@sst.scaler.com
- Roll no: 10259 (24BCS10259)

Report with the results table and analysis: **[REPORT.md](REPORT.md)**

## Files

| file | what |
|---|---|
| `common.h` | Side, Level, BBO structs + price/tick helpers |
| `book_a.h` | Implementation A - `std::map` + `std::unordered_map` |
| `book_b.h` | Implementation B - sorted `std::vector`s + binary search |
| `book_c.h` | Implementation C - flat array by tick + vector by order id |
| `bench.cpp` | benchmark (`--impl A/B/C`) and correctness test (`--verify`) |
| `run_perf.sh` | runs verify + bench + perf stat + perf record for all 3 |
| `results/` | raw output from my runs |

## Build

```
g++ -std=c++17 -O2 -o bench bench.cpp
```

## Run

Correctness test:
```
./bench --verify
```

Benchmark (1M ops, seed 42, 5 runs, prints median):
```
./bench --impl A
./bench --impl B
./bench --impl C
```
Optional: `--runs N` and `--ops N`.

perf (Linux only):
```
./run_perf.sh
```
or by hand:
```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A
perf record -g ./bench --impl A
perf report
```

## Interface changes

The API is the same as in the brief (`double` price, `long` qty). Changes inside:

- All three books convert price to an integer tick right away (`llround(price * 100)`) and only work with ticks. Prices go back to double only when building `BBO` / `Level`.
- `add_order` silently ignores bad input: price outside the side's band, qty <= 0, or a duplicate id.
- Each class is named `OrderBookA`, `OrderBookB`, `OrderBookC`. There is no base class; the benchmark uses templates so all three go through the same code.
- Book C stores orders in a vector indexed by id, so it expects small, mostly dense ids (like 1, 2, 3...). Huge ids will make it allocate a lot of memory. Order qty in C is stored as `int`.

## Data structure choices (short)

**A - map + hash map**
- levels: `std::map<int,long>` for each side (bids use `std::greater` so `begin()` is the best)
- orders: `std::unordered_map<id, {side, tick, qty}>`
- best bid/ask: `begin()` of each map
- cancel: hash lookup, then `map.find` and erase the level if qty hits 0

**B - sorted vectors**
- levels: `std::vector<{tick,qty}>` for each side, kept sorted so the best price is at the back
- orders: `std::vector<Order>` sorted by id. Ids come in increasing order so add is a `push_back`. Cancelled orders are only marked dead, never erased
- best bid/ask: `back()` of each vector
- cancel: binary search on the orders vector, then binary search on the level vector

**C - arrays (my design)**
- levels: one `long level_qty[10001]` indexed by `tick - 5000` (both sides, since bands don't overlap)
- orders: `std::vector<{tick,qty}>` indexed directly by id
- best bid/ask: cached in two ints. Updated on add with one compare. On cancel, only if the best level becomes empty, we walk the array to the next non-empty tick
- cancel: `orders[id]`, O(1)

| op | A | B | C |
|---|---|---|---|
| add_order | O(log L) | O(log L) search + O(L) insert of a new level, O(1) amortized order push | O(1) |
| cancel_order | O(1) avg hash + O(log L) | O(log N) + O(log L), O(L) if the level is erased | O(1), worst O(T) when the best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids/asks(d) | O(d) | O(d) | O(d + empty ticks skipped), worst O(T) |

N = orders, L = price levels on one side (max 5000), T = ticks on one side (5000).

More details and the perf analysis are in [REPORT.md](REPORT.md).

## Correctness output

```
Running correctness test...

Checked 1000000 operations (BBO + 10 levels each side after every op)

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

The test runs the same 1M op workload through all three books together and after **every** op compares the BBO, top 10 bids and top 10 asks (price + qty) and the cancel return values. A is the reference. It also runs some hand written cases from the spec: empty book, cancel unknown id, double cancel, only 3 levels when asking for 10, best price moving after a level empties.

Note: the checksum is different on macOS (libc++) and Linux (libstdc++) because `uniform_int_distribution` is implemented differently, so the workload is not exactly the same between the two. On the same machine it's always the same.
