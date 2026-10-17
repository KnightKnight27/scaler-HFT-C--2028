# Capstone: Low-Latency Order Book

Parv Mehta  
Roll no: 24BCS10301  
Email: parv.24bcs10301@sst.scaler.com

Three implementations of a single-instrument resting order book (no matching),
one benchmark program that drives all of them with the same random workload,
a correctness mode that proves they agree, and perf/cachegrind data with a
write-up of why they behave differently.

The performance analysis and results table are in [REPORT.md](REPORT.md).

## Layout

```
include/order_book.h      Side, Level, BBO, tick helpers (shared by everything)
include/map_book.h        Implementation A  - std::map + std::unordered_map
include/heap_book.h       Implementation B  - price heap with lazy deletion + hashed levels
include/array_book.h      Implementation C  - flat arrays + two-level bitmap + own hash table
src/map_book.cpp          A, B and C method definitions
src/heap_book.cpp
src/array_book.cpp
src/workload.h            deterministic random workload generator (mt19937, seed 42)
src/reference_book.h      brute-force oracle used by the correctness checks
src/bench.cpp             the benchmark / --verify program
src/tests.cpp             unit tests (edge cases + random cross-check vs the oracle)
results/                  outputs from my machine: verify.txt, bench_A/B/C.txt
perf/                     Dockerfile + scripts, perf/results/ with perf stat, perf report and cachegrind output
REPORT.md                 results table and performance analysis
```

## Building

Needs a C++17 compiler. Either

```
make                 # builds ./bench and ./tests with -O2
```

or a single command

```
g++ -std=c++17 -O2 -Iinclude src/bench.cpp src/map_book.cpp src/heap_book.cpp src/array_book.cpp -o bench
g++ -std=c++17 -O2 -Iinclude src/tests.cpp src/map_book.cpp src/heap_book.cpp src/array_book.cpp -o tests
```

or CMake

```
cmake -B build && cmake --build build       # binaries in build/
```

## Running

Correctness test (all three implementations on the same 1,000,000-op workload):

```
./bench --verify
make verify          # same thing
```

Unit tests (edge cases on all three, plus random workloads against the oracle):

```
./tests
make test
```

Benchmark one implementation, 5 runs, median reported:

```
./bench --impl A --repeat 5
./bench --impl B --repeat 5
./bench --impl C --repeat 5
./bench --impl all --repeat 5     # all three in one go
make bench-all                    # same
```

Useful options (`./bench --help` lists them all):

| option | meaning |
| --- | --- |
| `--ops N` | number of operations, default 1,000,000 |
| `--seed S` | RNG seed, default 42 |
| `--repeat N` | run N times, print every run and the median |
| `--timing total` | time only the whole loop, no per-op timers. This is what the perf scripts use. |
| `--timing perop` | only the per-op timed pass |
| `--band N` | use only the N best price levels on each side (experiments with a narrow, busy book) |
| `--csv` | append a machine readable summary line |

The benchmark prints the final BBO and a checksum that every result of every
query is folded into, so the compiler cannot optimise the work away. The
checksum is the same for A, B and C (and the same as in `--verify`).

### How the benchmark measures

The workload is generated *before* timing starts into a flat vector of ops, so
all implementations see exactly the same sequence and the RNG is never on the
timed path. By default the workload is run twice per repetition:

1. once with a single timer around the whole loop. This gives the "total
   time" and is the cleanest number;
2. once with `steady_clock::now()` around every call, to get the average and
   the p50/p90/p99/p99.9/max per operation type.

The per-op numbers include the cost of the two clock reads (printed as "timer
overhead"; `./bench --impl none` measures it in place at about 12 ns per
call on my Mac). On Apple Silicon `steady_clock` only ticks
every 41.7 ns (24 MHz), which is why the percentiles come out as multiples of
42. Averages over hundreds of thousands of samples are still fine; individual
percentiles below ~50 ns should be read as "under one tick".

## Profiling

I developed this on a Mac, where `perf` does not exist, so all perf data was
collected inside a Linux container (Debian, g++ 12) with the scripts in
`perf/`:

```
perf/docker_perf.sh      # builds the image and runs perf/run_perf.sh inside it
perf/run_perf.sh         # perf stat -r 5, perf record -g + perf report, cachegrind, for A/B/C
```

Docker Desktop's Linux VM does not expose the CPU's hardware counters, so
`perf stat` reports `cycles`, `instructions`, `branches`, `branch-misses`,
`cache-misses`, `L1-dcache-load-misses` and `dTLB-load-misses` as
`<not supported>`; `task-clock` and `page-faults` work, and `perf record`
samples with `cpu-clock`. To still be able to talk about cache and branch
behaviour I ran the same binary under cachegrind (`valgrind --tool=cachegrind
--branch-sim=yes`), which simulates the caches and the branch predictor and
reports misses per implementation. Those numbers are simulated, not measured,
and are labelled as such in the report. `perf/run_perf.sh` is plain Linux and
will fill in the hardware counters when run on a lab machine.

## Interface

The class interface is exactly the one from the assignment:

```cpp
void add_order(uint64_t id, Side side, double price, long qty);
bool cancel_order(uint64_t id);
BBO get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

Notes on behaviour that the assignment leaves open (all three behave the same,
the unit tests check it):

- Prices are converted to integer ticks (`lround(price * 100)`) at the API
  boundary and everything inside works in ticks. `Level` and `BBO` still
  return doubles.
- `add_order` ignores `qty <= 0`, prices outside the bid band [50.00, 99.99]
  or ask band [100.01, 150.00], and duplicate ids.
- `cancel_order` of an unknown or already cancelled id returns `false`.
- `get_bbo` on an empty side reports `has_bid`/`has_ask` false.
- `get_bids(n)` / `get_asks(n)` return at most n levels, fewer if the book has
  fewer. Bids are highest first, asks lowest first.
- Each class also has `order_count()` (used by tests) and a `name()`.
- Implementation C's constructor takes an optional `expected_orders` to size
  its order table up front (default 2^19). It still grows if exceeded.
- Implementation C reserves the id value `2^64 - 1` as its empty-slot marker.

## The three designs

### A: `MapBook` (std::map + std::unordered_map)

- Price levels: `std::map<Tick, LevelInfo>` per side, a red-black tree.
  Bids use `std::greater` so `begin()` is the highest price. `LevelInfo` is
  the total quantity and the number of orders at that price.
- Orders: `std::unordered_map<uint64_t, Order>` where `Order` is {side,
  tick, qty}.
- Best bid/ask: `begin()` of each map.
- Cancel: hash lookup of the id to learn the side/price/qty, then a tree
  lookup of the level; the level is erased when its order count hits zero.

| operation | complexity (L = live levels on that side, N = live orders) |
| --- | --- |
| add_order | O(1) avg hash insert + O(log L) tree insert/find |
| cancel_order | O(1) avg hash find/erase + O(log L) tree find (+ erase) |
| get_bbo | O(1) (`begin()` of a tree is cached by the implementation) |
| get_bids / get_asks | O(depth) in-order walk |

### B: `HeapBook` (binary heap of prices with lazy deletion)

- Best price: a binary heap (`std::push_heap`/`std::pop_heap` on a vector)
  holding every price that currently has orders. Max-heap for bids,
  min-heap for asks. A price is pushed when its level goes from empty to
  non-empty. When a level becomes empty it is *not* removed from the heap
  (that would be a linear search); it stays until it surfaces at the top,
  where it is discarded. `cancel_order` pops empty levels off the top right
  away, so the top of the heap is always a real level and `get_bbo` never
  has to clean up.
- Price levels: `std::unordered_map<Tick, LevelInfo>` per side (hashed, not
  ordered). `LevelInfo` holds the total quantity, an `in_heap` flag, and a
  vector of the orders resting at that price.
- Orders: live inside their level's vector as {id, qty}. A separate
  `std::unordered_map<uint64_t, Tick>` maps id to price so a cancel knows
  which level to search; the side follows from the price.
- Cancel: hash lookup id -> tick, hash lookup tick -> level, linear scan of
  the level's vector for the id, swap-remove it, then fix the heap top.
- Depth: a heap only gives you the top element cheaply, so `get_bids(10)`
  copies the heap and pops from the copy, skipping empty levels.

| operation | complexity |
| --- | --- |
| add_order | O(1) avg (two hash ops) + O(log L) heap push when a level is created |
| cancel_order | O(1) avg (two hash ops) + O(k) scan of the level (k = orders at that price) + O(log L) per empty level popped from the top |
| get_bbo | O(1): heap front + one hash lookup for the quantity |
| get_bids / get_asks | O(L + depth log L): copy the heap, then pop `depth` times |

### C: `ArrayBook` (my own design: flat arrays, bitmap, own hash table)

- Price levels: a plain `int32_t qty[5000]` per side. The index is computed
  from the tick (`tick - 5000` for bids, `15000 - tick` for asks) so both
  sides are "bigger index = better price" and share the same code. No
  lookup, no allocation, 20 KB per side.
- Best price: a two-level bitmap over that array. `occ` has one bit per
  level (79 x 64-bit words), `summary` has one bit per `occ` word (2 words).
  Finding the next best level after the best one is emptied is at most two
  `summary` reads plus one `occ` read, each resolved with a single `clz`
  instruction. The current best index is also cached, so `get_bbo` is two
  array loads.
- Orders: a hand-written open-addressing hash table. 16-byte slots of
  {id, qty, tick}, Fibonacci hashing (`id * golden ratio >> shift`), linear
  probing, and backward-shift deletion so there are no tombstones to skip
  over later. The table is allocated once up front (default 2^20 slots =
  16 MB); nothing is allocated per order.
- Cancel: one probe sequence in the hash table (usually one cache line),
  subtract from the level, clear the bit if the level is now empty, and
  only if that level was the best, consult the bitmap for the next one.

| operation | complexity |
| --- | --- |
| add_order | O(1): hash insert + array add + bit set |
| cancel_order | O(1) expected: hash erase + array subtract, and a constant-bounded bitmap search when the best level empties |
| get_bbo | O(1): two loads |
| get_bids / get_asks | O(depth) bitmap walk |

## Results summary

Everything below is from my machine (Apple M5 Pro, macOS, Apple clang 21,
`-O2`), 1,000,000 operations, seed 42, median of 5 runs. Full output is in
`results/`, perf data in `perf/results/`, analysis in `REPORT.md`.

| metric | A (map) | B (heap) | C (arrays) |
| --- | ---: | ---: | ---: |
| total time (ms) | 73.8 | 65.5 | 10.5 |
| ns / add | 63 | 41 | 15 |
| ns / cancel | 105 | 118 | 14 |
| ns / get_bbo | 12 | 13 | 13 |
| ns / get_bids(10)+get_asks(10) | 214 | 1147 | 194 |
| add p99.9 / max (ns) | 167 / 5,253,083 | 333 / 3,341,625 | 84 / 17,625 |

(per-op numbers include the timer overhead, about 12 ns on average as measured by `--impl none`; see above)

Correctness (`results/verify.txt`):

```
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```
