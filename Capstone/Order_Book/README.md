# Low-Latency Order Book (C++17)

Capstone project: one order book, three different data-structure designs, a benchmark, and a `perf` analysis.

| | |
|---|---|
| Name | Shreyash Sukahdev Kawde |
| Roll no. | 10253 |
| SST email | shreyash.24bcs10253@sst.scaler.com |

## Files

```
src/types.hpp           shared types (Side, Level, BBO, price bands)
src/book_a_map.hpp      Implementation A
src/book_b_vector.hpp   Implementation B
src/book_c_array.hpp    Implementation C (my own design)
src/workload.hpp        builds the random 1M operation workload
src/verify.hpp          correctness test
src/bench.cpp           the single benchmark program (main)
scripts/run_all.sh      rebuilds and regenerates everything in results/
results/                raw output: correctness, benchmark, perf stat, perf report
screenshots/            terminal screenshots of the runs
REPORT.md               results table + performance analysis
```

## Build

Needs g++ with C++17. Only the standard library is used.

```
g++ -std=c++17 -O2 -o bench src/bench.cpp
```

(or `cmake -S . -B build && cmake --build build`, the binary is written to the project folder)

The scripts and screenshots use `-O2 -g -fno-omit-frame-pointer` so `perf record -g` can show readable call graphs.

## Run

Correctness test (all three implementations on the same workload, compared after every operation):

```
./bench --verify
```

Benchmark, one implementation at a time (1,000,000 ops, seed 42, 5 runs, median reported):

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Other options: `--runs N`, `--ops N`, and `--total-only` (skips the per-operation timing pass, used for `perf`).

perf (the CPU here has P and E cores, so everything is pinned to P-core 0):

```
taskset -c 0 perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A --runs 1 --total-only

taskset -c 0 perf record -g ./bench --impl A --runs 1 --total-only
perf report
```

The benchmark program normally repeats the workload 5 times, so for `perf` I use `--runs 1` (perf does the 5 repeats with `-r 5`) and `--total-only` (no clock calls inside the loop, so the counters are not polluted). The CPU is hybrid, so counters named `cpu_atom/...` show `<not counted>`; all the `cpu_core/...` ones work.

`bash scripts/run_all.sh` does all of the above for A, B and C and saves the output in `results/`.

## Interface changes

- **Prices are integer ticks** (`int`), not `double`. 100.01 is `10001`. `ticks_to_price()` converts back when printing. Valid ticks: bids 5000..9999, asks 10001..15000.
- `add_order` returns `bool` instead of `void`: `false` if it was rejected (price outside its band, qty <= 0, or the id already exists). Nothing changes in the book when it is rejected.
- Each implementation is a separate class (`MapBook`, `VectorBook`, `ArrayBook`) with the same five functions. The benchmark uses templates, so there are no virtual calls.
- `Level` and `BBO` use `int` for the price, the rest is as in the assignment.

## The three designs

| | A: `MapBook` | B: `VectorBook` | C: `ArrayBook` (mine) |
|---|---|---|---|
| Price levels | `std::map<tick, qty>` per side | sorted `std::vector` per side | one `long qty[15001]` array indexed by tick |
| Orders | `std::unordered_map<id, Order>` | `std::map<id, Order>` | open addressing hash table, order stored in the slot |
| Best price | first element of the map | last element of the vector | cached `best_bid_`/`best_ask_`, bitmap search when the best level empties |
| Find order on cancel | hash lookup, then tree lookup of the level | tree lookup, then binary search of the level | hash lookup, then direct array index |

Complexity (L = price levels per side, at most 5000; N = live orders; d = depth):

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | O(log L) average O(1) hash | O(log N + L) worst, the vector insert shifts | O(1) average |
| `cancel_order` | O(log L) average O(1) hash | O(log N + L) worst, the erase shifts | O(1) average, plus a bounded bitmap search if the best level empties |
| `get_bbo` | O(1) | O(1) | O(1) |
| `get_bids` / `get_asks` | O(d) | O(d) | O(d) plus bitmap steps |

More detail on why they perform differently is in [REPORT.md](REPORT.md).

## Results

- Correctness output: `results/correctness.txt`
- `perf stat` output for A, B and C: `results/perf_stat_A.txt`, `_B`, `_C`
- Results table and written analysis: [REPORT.md](REPORT.md)
- Terminal screenshots (prompt `shreyashkawde@shreyashkawde-750XGK:~$`) in `screenshots/`:
  `01` build + verify, `02-04` benchmark A/B/C, `05-07` perf stat A/B/C, `08-10` perf record + report (text), `11-13` perf report (interactive)
