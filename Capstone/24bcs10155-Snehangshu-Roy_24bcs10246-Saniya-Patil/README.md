# Capstone: Low-Latency Order Book

Team:
- Snehangshu Roy, 24BCS10155, snehangshu.24bcs10155@sst.scaler.com
- Saniya Patil, 24BCS10246, saniya.24bcs10246@sst.scaler.com

We built the same single-instrument order book three times with different data structures, plus
one program that benchmarks all three and checks that they agree. The results table and our
analysis are in [REPORT.md](REPORT.md).

## Files

| file | what's in it |
|---|---|
| `common.hpp` | `Side`, `Level`, `BBO`, tick constants, price check shared by all three books |
| `map_book.hpp` | Implementation A: `std::map` for levels + `std::unordered_map` for orders |
| `heap_book.hpp` | Implementation B: `std::priority_queue` with lazy deletion + `std::map` for orders |
| `array_book.hpp` | Implementation C (our design): flat array of levels + bitmap + our own hash table |
| `workload.hpp` | the random op generator (fixed seed) and the loop that plays it on a book |
| `verify.hpp` | `--verify`: edge cases, a brute force reference book, random workloads |
| `bench.cpp` | `main` and the benchmark (`--impl A/B/C`) |
| `Makefile` | `make` builds `bench`, `make verify` runs the correctness test |
| `run_perf.sh` | builds, runs verify, the benchmark and perf for A/B/C, writes `results/` |
| `per_run_counters.sh` | perf counters for one run of the book only (see REPORT section 3) |
| `perf.Dockerfile` | the Ubuntu image we used for g++ and perf |
| `results/` | the raw output of our final run, everything in the report comes from here |

## Build

We used g++ 13.3 (clang++ works too).

```
g++ -std=c++17 -O2 -Wall -Wextra -g -fno-omit-frame-pointer -o bench bench.cpp
```

or just `make`. Plain `g++ -std=c++17 -O2 -o bench bench.cpp` works as well. `-g` only adds debug
info. `-fno-omit-frame-pointer` keeps a frame pointer so `perf record -g` gets proper call stacks.
That changes the code a tiny bit (one register less), but it's still the -O2 build.

Only the standard library is used. C also uses `__builtin_clzll` / `__builtin_ctzll` (GCC/Clang
builtins, not a library) to find set bits in its bitmap.

The ops come from `std::uniform_int_distribution`, which each standard library implements its own
way. So you get our exact op counts and checksum only with libstdc++ (g++, or clang on Linux).
With libc++ (macOS) the workload and the checksum come out different, but `--verify` still passes.

## Correctness test

```
./bench --verify
```

What it checks (code in `verify.hpp`):

1. Edge cases on each book: empty book, cancelling an unknown id or the same id twice, bad prices
   (outside the band, the mid, off the 0.01 grid, NaN, inf), bad qty, duplicate id, id 0 and the
   max id, fewer levels than the asked depth, depth 0, negative and default, a middle level
   emptying, a level coming back after it emptied.
2. C's own hash table against `std::unordered_map` (9 x 200k random inserts/erases, starting from
   16 slots so it has to grow a lot).
3. Four random workloads of 200k ops: the normal 70/20/10 mix, a "thin" book where adds ~=
   cancels so levels keep emptying, random 64 bit ids instead of 1, 2, 3, ..., and random ids
   with all prices within 20 ticks of the mid. 5% of the cancels go to unknown or already
   cancelled ids. Each workload is played on A, B, C and a brute force reference book side by
   side (the reference is just an array of qty per tick and a full scan for every query). After
   every op all three are compared with the reference: what add/cancel returned, the BBO, the top
   10 bids and asks, and the bids and asks at a second depth that changes every op (-2 to 22, and
   the whole book every 1000 ops).
4. The real 1M op benchmark workload, checked the same way, and the checksums have to match.

It takes about 10 seconds. Our output (`results/verify.txt`):

```
Running correctness test...

1. edge cases (empty book, bad prices/qty, unknown/double cancel, depth, emptied levels): ok
2. C's OrderTable vs std::unordered_map, 9 x 200000 random inserts/erases: ok
3. random workloads, A, B, C and a brute force book side by side, compared after every op
   (5% of cancels go to unknown or already cancelled ids)
   a) normal 70/20/10 mix: 200000 ops (add 140191, cancel 39805, bbo 20004), seed 7
   b) thin book, 45% add / 45% cancel: 200000 ops (add 90067, cancel 89931, bbo 20002), seed 8
   c) random 64 bit ids: 200000 ops (add 140170, cancel 39925, bbo 19905), seed 9
   d) random ids, thin book, only 20 ticks per side: 200000 ops (add 89674, cancel 90430, bbo 19896), seed 10
4. the benchmark workload itself (1M ops, seed 42), compared after every op like in 3
   checksum: A=13740035995088966065 B=13740035995088966065 C=13740035995088966065 (match)

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

We also built it with `-Wall -Wextra -Wpedantic` and with `-fsanitize=address,undefined` (g++ and
clang++) and ran `--verify` with those builds: no warnings, no errors.

## Benchmark

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Optional flags: `--runs N` (default 5), `--ops N` (default 1,000,000), `--seed S` (default 42),
`--no-timers` (skip the per-op timing, only total time).

The workload is 1M ops made up front with `std::mt19937(42)`: ~70% add, ~20% cancel, ~10%
`get_bbo`, plus `get_bids(10)` and `get_asks(10)` every 100 ops. Side is 50/50, qty is 1-100,
the price is a random tick in the band for that side, and a cancel always picks an order that is
live at that point (the generator keeps a list of live ids). All three books get the exact same
op list.

Each run does two passes, each on a fresh book:
- pass 1 has no timers in the loop and gives the total time and the checksum
- pass 2 puts `steady_clock::now()` around every add and cancel and gives the average ns for each.
  The cost of one `now()` call (~19 ns here) is measured at the start and subtracted, and the
  raw number is printed too. One `get_bbo` is quicker than a clock read, so for every BBO op we
  time 16 `get_bbo` calls back to back and divide by 16. There's a compiler barrier
  (`std::atomic_signal_fence`) between the calls so the compiler can't merge them. We looked at
  the objdump output for all three books: the batch is a loop that calls `get_bbo` 16 times, and
  each time it reads the best price from the book again and does the tick to price division.

It does 5 runs and prints the medians. At the end it prints the final BBO and a checksum (made
from every cancel result, every BBO and every depth snapshot) so the compiler can't throw any of
the work away. Example (`results/bench_C.txt`):

```
median of 5 runs:
  total time      33.37 ms  (33.4 ns per op, untimed loop)
  ns / add         56.7  (raw 75.6)
  ns / cancel      38.3  (raw 57.2)
  ns / bbo         1.48  (raw 2.66, timed in batches of 16 calls)

Final BBO:
Bid: 99.99 x 1981
Ask: 100.01 x 2573
Live orders: 501168

Checksum: 13740035995088966065
```

## perf

We're on Windows, so everything Linux ran in Docker Desktop (WSL2 kernel 6.6, Ubuntu 24.04,
perf 6.8). All the counters the assignment asks for were available.

```
docker build -t ob-perf -f perf.Dockerfile .
docker run --rm --privileged -v "$(pwd):/work" ob-perf ./run_perf.sh
```

(From Git Bash on Windows we needed `MSYS_NO_PATHCONV=1` and `"$(pwd -W):/work"`.)

`run_perf.sh` runs this for each of A, B and C:

```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X
perf record -g ./bench --impl X
perf report
```

and saves everything in `results/` (so running it overwrites our results). It also does the same
perf stat with `--no-timers` (`perf_stat_X_no_timers.txt`), runs `per_run_counters.sh` (counters
for one run of the book alone, see REPORT section 3), and the two page fault experiments from
REPORT 11.4 (`page_faults.txt`, `experiment_presize.txt`).

## Interface changes

We kept the API from the assignment (double prices in, double prices out), with these changes:

- `add_order` returns `bool` instead of `void`. It returns false and leaves the book alone if the
  price isn't on the 0.01 grid, is outside the band for that side (bids 50.00-99.99, asks
  100.01-150.00), the qty isn't between 1 and 2^31-1, or the id is already live.
- Inside, every book works in integer ticks (`price * 100`, `llround`), so the data structures
  never compare doubles. The price is converted and checked once in `add_order` (`price_to_tick`
  in `common.hpp`) and converted back only when a `BBO` / `Level` is returned.
- The `BBO` fields have default values, so an empty side is `has_x = false, price 0, qty 0`.
- Each book also has `size_t order_count() const` (used to print the final state and in one
  `--verify` check).
- The classes are called `MapBook` (A), `HeapBook` (B) and `ArrayBook` (C) instead of `OrderBook`,
  and all three have the same 5 API functions with the same signatures (the assignment's, except
  `add_order` returns `bool`). There's no virtual base class:
  the benchmark and verify code are templates (`play<Book>`, `run_once<Book>`, `edge_cases<Book>`),
  so all three go through exactly the same code without a virtual call per op.

## Data structure choices (short version)

| | A | B | C |
|---|---|---|---|
| orders | `std::unordered_map<id, Order>` | `std::map<id, Order>` | our own open addressing table, 16 byte slots, linear probing |
| price levels | `std::map<tick, qty>` per side | `std::unordered_map<tick, {qty, in_heap}>` per side | `long qty[10001]` indexed by tick - 5000 |
| best bid/ask | `begin()` of the map | top of `std::priority_queue`, stale ticks popped on cancel | cached index, bitmap scan when the best level empties |
| cancel lookup | chained hashing: bucket, then a pointer to a separately allocated node | tree search, ~19 levels for 500k orders | open addressing: probe 16 byte slots in one array, the order data is in the slot |

| op | A | B | C |
|---|---|---|---|
| add_order | O(log P) | O(log N) | O(1) amortized |
| cancel_order | O(log P) | O(log N) + amortized O(log P) | O(1) expected, O(P/64) worst |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(d) | O(P + (d + s) log P), O(P log P) worst | O(d + P/64) worst |
| get_asks | O(d) | O(P + (d + s) log P), O(P log P) worst | O(d + P/64) worst |

N = live orders, P = price levels per side (at most 5000), d = depth, s = stale ticks skipped.
More detail in [REPORT.md](REPORT.md#1-the-three-implementations).

None of the books keep a FIFO list of orders per level, only the total qty. There's no matching
in this assignment and the API only returns totals per level, so time priority never shows up
(REPORT 11.6 says what we'd add for matching).
