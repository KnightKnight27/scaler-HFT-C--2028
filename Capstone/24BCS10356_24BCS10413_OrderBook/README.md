# Low-Latency Order Book — Capstone (Intermediate C++)

**Team**

| Name | Roll number | SST email |
|---|---|---|
| Shambhu Yadav | 24bcs10356 | shambhu.24bcs10356@sst.scaler.com |
| Ratnesh Vaibhav | 24bcs10413 | ratnesh.24bcs10413@sst.scaler.com |

Three genuinely different single-instrument resting order books behind the same
interface, one benchmark program that selects the implementation on the command
line, a lock-step correctness test (`--verify`) against an independent reference
model, a regression/differential test program, and a perf-based analysis
(`REPORT.md`).

```
include/order_book_common.hpp   Side, Level, BBO, tick helpers (shared)
include/book_a_map.hpp          Implementation A : std::map levels + std::unordered_map orders
include/book_b_vector.hpp       Implementation B : sorted std::vector of levels, orders stored in the level, std::map id lookup
include/book_c_array.hpp        Implementation C : price-indexed arrays + bitmap + custom open-addressing hash
src/workload.hpp                deterministic workload generator (mt19937(42))
src/reference_book.hpp          naive reference model (tick array + linear scan) used as the test oracle
src/bench.cpp                   benchmark + --verify + --perf + --gen-only
tests/test_orderbook.cpp        regression + randomized differential tests against the reference model
scripts/perf_all.sh             runs perf stat / perf record for A, B, C (Linux)
scripts/bench_all.sh            runs tests, --verify and the A/B/C benchmarks, saves outputs
results/                        Linux (EC2 c7g.large, Arm) verify, bench and perf outputs; results/macos/ has Mac runs
results/x86/                    AMD Ryzen 5 5600H (x86-64) tests, verify and bench outputs from the current code
REPORT.md                       results table + performance analysis
```

## Build

Single compiler command (C++17, `-O2`):

```sh
g++ -std=c++17 -O2 -Wall -Wextra -g -fno-omit-frame-pointer -Iinclude -Isrc src/bench.cpp -o bench
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -Isrc tests/test_orderbook.cpp -o test_orderbook   # tests
```

or simply

```sh
make            # builds bench and test_orderbook (the two commands above)
make test       # builds both, runs test_orderbook, then ./bench --verify
make sanitize   # the tests + a 200k-op --verify under AddressSanitizer + UBSan
# or
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build   # build/bench, build/test_orderbook
(cd build && ctest)                                                 # unit tests + --verify
```

`-g` and `-fno-omit-frame-pointer` only add symbols / keep frame pointers so
that `perf record -g` gives readable call graphs. Code generation is plain `-O2`.
No external libraries; the standard library only. (`__builtin_ctzll` in C and the
empty `asm volatile` barrier in the benchmark are GCC/Clang compiler built-ins, not
libraries.)

## Run the correctness test

```sh
./bench --verify             # 1,000,000 ops, seed 42
./bench --verify --ops 200000 --seed 7
```

The test (1) runs hand-written edge cases on every implementation (empty-book
BBO, cancel of an unknown id, double cancel, `get_bids(10)` on a 3-level book,
BBO moving to the next level when the best level empties, re-using an emptied
level, zero / negative / huge depth requests, rejection of out-of-band,
off-tick, wrapping and NaN prices, qty <= 0, duplicate ids, and the largest
id 2^64-1) and (2) feeds the identical random workload to A, B and C in
lock-step with `ReferenceBook` (`src/reference_book.hpp`), a deliberately
naive model that keeps the quantity per tick in a plain array and finds the
BBO and depth by scanning it. After **every** operation it compares each book
with the reference: the `add_order`/`cancel_order` return value, the BBO,
`get_bids(10)` and `get_asks(10)` (price and quantity of every level). Because
each book is checked against the reference rather than against A, a bug in A
fails A alone. It also checks that the benchmark path produces the same
checksum for the reference and all three books.

Captured output on Linux (`results/x86/verify.txt`; the checksum matches the
Graviton run in `results/verify.txt` because both use libstdc++):

```
Running correctness test...

Edge cases (empty book, unknown/double cancel, insufficient depth, invalid input):
  A: PASS   B: PASS   C: PASS
Lock-step workload comparison against an independent reference model (1010000 ops;
add/cancel result, BBO and 10-level bid/ask depth compared after every op):
  mismatching ops: A 0, B 0, C 0
Benchmark checksums: reference=9406903409647291425 A=9406903409647291425 B=9406903409647291425 C=9406903409647291425

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

### Regression and differential tests

```sh
make test               # or: ./test_orderbook
```

`tests/test_orderbook.cpp` covers inputs the benchmark workload never produces:
all 10,000 valid prices through the `double` API (as `tick / 100.0` and parsed
with `strtod`), whole-number prices passed as `int` / `long` / `unsigned`, a full
5,000-level side, wrapping / off-tick / NaN / infinite / wrong-band prices, draining a full book in random order (the best level keeps
moving, so C scans its bitmap across word boundaries), and 3 x 200,000 random
operations with random 64-bit ids (including 0 and 2^64-1), duplicate adds,
cancels of unknown and already-cancelled ids, re-use of a cancelled id and
invalid quantities, each compared with the reference model. It also runs C
starting from a 16-slot hash table so that `grow()` and wrap-around
backward-shift deletion execute (`--verify`'s 2^21-slot table never grows).
Output (`results/x86/tests.txt`): `120156 checks, 0 failures`. `make sanitize`
runs the tests and a 200k-op `--verify` under ASan + UBSan; both are clean.

## Run the benchmarks

```sh
./bench --impl A         # 5 runs, median reported
./bench --impl B
./bench --impl C
./bench --impl C --runs 3 --ops 2000000 --seed 1
```

One program, one workload (`src/workload.hpp`), implementation chosen with
`--impl`. The workload is generated **before** timing starts, so RNG cost and
the live-order bookkeeping needed to pick cancel targets are never timed.

Workload (seed 42): 1,000,000 operations, ~70 % add / 20 % cancel / 10 %
`get_bbo`, side 50/50, qty 1–100, uniformly random valid tick in the band, every
cancel targets a currently-live order (if nothing is live the slot becomes an
add; with seed 42 on libstdc++ this never happens, and in general it can only
happen in the first few operations). After every 100 operations one
extra "depth" operation calls `get_bids(10)` + `get_asks(10)` (10,000 of them,
on top of the 1,000,000).

The checksum depends on the C++ standard library. `std::mt19937` itself is fully
specified, but `std::uniform_int_distribution` is implemented differently by
libstdc++ (GCC, Linux) and libc++ (Apple clang), so the same seed produces a
different op stream on each. All three implementations always run the identical
stream on a given machine, so the comparison is unaffected. For reference, seed 42
gives checksum `9406903409647291425` with GCC 13 / libstdc++ and
`6207609776843994971` with Apple clang 17 / libc++.

Each run does two passes over the same op stream:

1. **uninstrumented** — one `steady_clock` read before and after the loop →
   *Total time*;
2. **instrumented** — a `steady_clock` pair around every single call →
   mean ns / add, ns / cancel, ns / bbo, ns / depth, plus p50 / p90 / p99 /
   p99.9 / max latency per operation type (median over the runs). The cost a
   clock pair adds to every sample (the mean *empty* timed interval, ≈24 ns on
   the Ryzen) is measured the same way and printed; the "net" column subtracts
   it, and a "timer only" row in the percentile table shows the floor.
   Because a `get_bbo` is far cheaper than a clock read, a third number is
   reported: 1,000,000 back-to-back `get_bbo()` calls timed as a block, behind
   a compiler barrier so each iteration really calls `get_bbo()`.

Page faults are read with `getrusage` around each phase and reported as
workload generation / book construction / timed loop (run 1).

The final BBO and a checksum (folded from every BBO/depth result and every
cancel return value) are printed so the compiler cannot discard the work.

`./scripts/bench_all.sh results/<name>` runs the tests, `--verify` and all three
benchmarks and saves the outputs with a machine summary
(`PIN="taskset -c 3"` pins the benchmarks to one core).

**Measurement fixes in this revision** (the Graviton files in `results/`
predate them; `REPORT.md` §3 explains the effect): an earlier version
subtracted the wall time of a whole timing-loop iteration (two clock reads)
instead of one timed interval, so its "net" figures were about one clock read
too low; and its `get_bbo` burst folded each result through `lround()`, which
cost ~3x more than `get_bbo` itself.

For `perf`, use `--perf`: one uninstrumented pass, no timers, no bbo burst, so
the counters are dominated by the book itself:

```sh
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A --perf
perf record -g ./bench --impl A --perf && perf report
./scripts/perf_all.sh      # all of the above for A, B and C, plus the --gen-only baseline
perf stat -r 5 -e ... ./bench --gen-only   # baseline: workload generation alone
```

perf counts the whole process, including generating the workload. Running the
same `perf stat` on `./bench --gen-only` measures that shared cost alone, so it
can be subtracted from each implementation's counters; `perf_all.sh` saves it
as `perf_stat_gen.txt`.

## Interface changes

The required API is kept, with these additions / notes:

* **Integer ticks.** Internally every implementation stores prices as `int`
  ticks (1 tick = 0.01, `100.01 → 10001`). `add_order(id, side, double price,
  qty)` is still provided (see `price_to_tick()` below); each class also has
  `add_order_ticks(id, side, int tick, qty)`, which is what the benchmark
  calls, so no float conversion sits inside the timed region. `Level.price`
  and `BBO.*_price` are still `double` (`tick / 100.0`).
* The tick entry point has its own name on purpose. It used to be a second
  `add_order` overload, so `add_order(1, Side::Buy, 75, 10)` picked the `int`
  overload, meant 75 *ticks* (0.75) and was rejected, and a `long` price did
  not compile (ambiguous call). Whole-number prices now mean what they say:
  `add_order(1, Side::Buy, 75, 10)` rests at 75.00.
* `Side`, `Level`, `BBO` are exactly as specified (`order_book_common.hpp`).
* The three classes are `OrderBookA`, `OrderBookB`, `OrderBookC`; they are
  used through templates (not a virtual base) so the benchmark pays no
  virtual-call cost that would blur the comparison.
* `add_order` returns `bool` instead of `void`. It returns `false` and leaves
  the book unchanged when the price is outside its side's band (bids
  50.00–99.99, asks 100.01–150.00), when `qty <= 0` or `qty > INT32_MAX`, or
  when the id is already live. This keeps C's price arrays from being indexed
  out of bounds and keeps all three books in agreement on bad input.
* `add_order(..., double price, ...)` converts with `price_to_tick()`, which
  also rejects prices that are not a multiple of 0.01 (more than 1e-8 away
  from a tick, e.g. `100.013`), NaN / infinity, and values far outside the int
  range. Previously `static_cast<int>(lround(price * 100))` silently rounded
  off-tick prices and wrapped huge ones: a buy at `42949772.95`
  (2^32 + 9999 ticks) was accepted at 99.99. All 10,000 valid prices still
  convert exactly, including ones parsed from text or accumulated by repeated
  `+= 0.01`.
* Every `uint64_t` id is accepted, including 2^64-1, which C stores in a spare
  slot because it doubles as its hash table's empty marker (previously C
  rejected it while A and B accepted it).
* Implementation C stores the quantity of an order as `int32_t` inside its
  16-byte hash slot (quantities are 1–100 here); level totals are `long`.

## Data-structure choices

| | A — tree | B — sorted vector, level-centric | C — price-indexed arrays (own design) |
|---|---|---|---|
| Orders | `std::unordered_map<id, {tick, qty, side}>` | `std::vector<{id, qty}>` **inside each price level**; `std::map<id, {tick, side}>` (tree) only says which level | custom open-addressing hash table, linear probing, backward-shift delete, 16-byte slots `{id, tick, qty}` |
| Price levels | `std::map<tick, qty>` per side (red-black tree) | `std::vector<PriceLevel>` per side, kept sorted, best at the back | `long qty[5000]` per side indexed by tick rank + 5000-bit occupancy bitmap |
| Best price | `map.begin()` | `vector.back()` | cached `best` rank; on emptying, `ctz` scan of the bitmap |
| Cancel lookup | hash → tick → `map.find` | id tree → tick → binary search → linear scan in level | probe → tick → direct array index |
| add | O(1) avg + O(log L) | O(log N) + O(log L), + O(L) element moves for a new level | O(1) avg |
| cancel | O(1) avg + O(log L) | O(log N) + O(log L) + O(k), + O(L) element moves when the level empties | O(1) avg, + up to 79 bitmap words when the best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids/asks(d) | O(d) | O(d) | O(d + 79) worst (bitmap words scanned) |

L = non-empty price levels on the side (≤ 5000), N = live orders, k = orders at one level.
C's bitmap scans are bounded by the 5,000-slot price range (79 words of 64 bits), not by L.
"avg" is the expected cost of a hash lookup.

### Design details

**A — ordered tree.** Orders live in `std::unordered_map<id, {tick, qty, side}>`. Levels are
`std::map<int, long>` per side; bids use `std::greater` so `begin()` is the best bid on both
sides. Cancel: hash find → `map.find(tick)` → subtract → erase the node if it reaches zero.

**B — sorted vector, level-centric.** Each side is a `std::vector<PriceLevel>` sorted so the
best price is at the back (bids ascending, asks descending). Each `PriceLevel` holds its tick,
its total, and a `std::vector<{id, qty}>` of the orders resting there. A `std::map<id, {tick, side}>`
(red-black tree keyed by id) records which level an order is in. Cancel: tree find → binary
search → linear scan of the level → swap-remove → erase the level if empty.

**C — price-indexed arrays (own design).** Each side is `long qty[5000]` indexed by rank
(0 = best possible price: bids `9999 - tick`, asks `tick - 10001`) plus a 79-word bitmap of
non-empty levels and a cached `best` rank. When the best level empties, the next one is found
with `__builtin_ctzll` over the bitmap. Orders live in a custom open-addressing hash table:
16-byte slots `{id, tick, qty}`, Fibonacci hashing, linear probing, load factor ≤ 0.5 and
backward-shift deletion (no tombstones). The side is implied by the tick. The table is
allocated and filled once at construction, so `add_order`, `cancel_order` and `get_bbo` never
allocate afterwards (the table only grows past 2^20 live orders). `get_bids`/`get_asks` return
a `std::vector` by value, as the API requires, so they allocate in all three designs.

## Collecting perf data on Linux

```sh
make
./scripts/bench_all.sh results/local   # tests, --verify and the A/B/C benchmarks
./scripts/perf_all.sh results/local    # perf_stat_{A,B,C,gen}.txt, perf_report_{A,B,C}[_flat].txt
```

Give each machine its own directory: `results/` itself holds the Graviton files and
`results/x86/` the Ryzen ones, and both scripts overwrite what is in the directory they are given.

`perf_all.sh` checks first that perf can open events and, if not, prints the
`perf_event_paranoid` fix instead of failing five times.

If `perf` reports permission errors, run `sudo sysctl kernel.perf_event_paranoid=1` (or ask the
lab admin). Events shown as `<not supported>` should be listed as unavailable in REPORT.md.
(On the Ryzen laptop used for `results/x86/`, `perf_event_paranoid` is 4 and root was not
available, so no hardware counters were collected there; see REPORT.md §3.)
