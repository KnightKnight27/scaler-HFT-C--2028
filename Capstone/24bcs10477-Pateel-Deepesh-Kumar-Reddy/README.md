# Low-Latency Order Book (Capstone)

**Name:** Pateel Deepesh Kumar Reddy
**SST email :** deepesh.24bcs10477@sst.scaler.com
**Roll number :** 24bcs10477

A resting order book for a single instrument, written three times with three
different data structures, plus one benchmark program that checks all three
agree. C++17, standard library only, single-threaded, no matching or trades.

## Build

```bash
g++ -std=c++17 -O2 -Wall -Wextra -o bench bench.cpp
```

## Run

```bash
./bench --verify     # correctness test; exit 0 means all three agree
./bench --impl A     # timing for implementation A (also use B, C)
```

## Correctness test

```
$ ./bench --verify
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

`--verify` checks two things:

1. Hand-written edge cases on each book: cancelling an unknown id, cancelling
   the same id twice, an empty book, a depth request larger than the number of
   levels, two orders at one price, zero/negative depth, ask ordering,
   promoting the next best bid after cancelling the best, and both ends of both
   price bands.
2. A differential check: B and C are compared against A after every operation
   (cancel return value, BBO, and bid/ask levels with quantities).

All three also produce the same checksum (`2472007972`) and the same final BBO
(`Bid 99.99 x 2722`, `Ask 100.01 x 2525`) over the full 1,000,000-op workload.

## Interface changes

1. Prices are integer ticks, not `double` (allowed by the assignment). `Level::price`,
   `BBO::bid_price` and `BBO::ask_price` are `int` tick counts, where
   100.01 maps to `10001`.
2. The five required methods keep their names, signatures and semantics.

Constraint on C: it indexes orders directly by id, so it is only fastest while
ids stay below `MAX_ID` (`1 << 21`). `add_order` grows the array if a larger id
arrives, so C stays correct for any id, just slower. A and B have no such limit.

## Data structures

| | A - `std::map` | B - sorted `std::vector` | C - fixed array |
|---|---|---|---|
| Levels | `map<int,long,greater<int>>` bids, `map<int,long>` asks | one sorted `vector<Level>` per side | `array<long,5000>` per side, indexed `price - base` |
| Orders | `unordered_map<id,Order>` | `map<id,Order>` | flat `vector<Slot>` by id, with a `live` flag |
| Best price | `begin()` | `back()` | cached `bid_best_` / `ask_best_` |
| Cancel locates order by | hash lookup then map find | tree lookup then binary search | direct index `orders_[id]` |
| Empty level | erased at qty 0 | erased at qty 0 | slot set to 0 |

A is the textbook choice: `begin()` is always the best price and there is no
shifting, but every level is a separate allocation reached by chasing pointers.

B keeps levels contiguous so a depth scan walks adjacent memory. The best price
is at the back of the vector. The cost is that a mid-vector insert or erase
shifts the tail, and its orders live in a `std::map`, so a cancel walks a tree.

C uses the fact that there are only 5,000 levels per side: a level is an array
slot at `price - base`, so finding it is a subtraction rather than a search. The
best price is cached and only recomputed when the cached best level is emptied.

The three differ on all four axes the assignment lists: order store
(`unordered_map` / `std::map` / flat array), level store (`std::map` / sorted
vector / fixed array), best-price lookup (`begin()` / `back()` / cached index),
and how a cancel finds its order (hash probe / tree search / direct index).

### Complexity (`L` = live levels on a side, at most 5000; `N` = live orders)

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | O(log L) | O(log L) + O(log N) + O(L) shift | O(1) |
| `cancel_order` | O(log L) | O(log N) + O(log L) + O(L) shift | O(1) typical, O(L) worst-case rescan |
| `get_bbo` | O(1) | O(1) | O(1) |
| `get_bids` / `get_asks` | O(depth) | O(depth) | O(depth + empty slots scanned) |

C's `cancel_order` is O(1) unless it empties the cached best level, which forces
a linear walk to the next non-empty level. B's O(L) term is the shift of the
level vector's tail; its O(log N) term is the order tree.

## Results

Timing, perf counters and the analysis are in [REPORT.md](REPORT.md). Raw output
is in `results/`.

## perf

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A
# repeat for B and C

perf record -g ./bench --impl A
perf report
# repeat for B and C
```

`perf stat` output is in `results/perf_stat_*.txt`. `perf report` output is in
`results/perf_record_*.txt`, listing each symbol with 1% or more self time.
This CPU is hybrid, so `perf` splits each counter into `cpu_core/*` (P-cores)
and `cpu_atom/*` (E-cores). Do not sum the two rows: they are the two halves of
one total. The report uses the `cpu_core` row. `cpu_atom/L1-dcache-load-misses`
is not supported here; the `cpu_core` equivalent is used.
