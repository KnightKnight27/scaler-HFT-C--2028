# Capstone - Low Latency Order Book

Name: Vansh Dobhal
Roll no: 24BCS10099
Email: vansh.24bcs10099@sst.scaler.com

Single instrument resting order book (no matching), written 3 times with different
data structures, plus one benchmark program that runs the same workload on all of them.

The full analysis (results table + answers to section 11) is in [REPORT.md](REPORT.md).

## Files

| File | What it is |
|------|------------|
| types.hpp | Side, Level, BBO, price <-> tick helpers, price band limits |
| book_map.hpp | Implementation A - std::map levels + unordered_map orders |
| book_vector.hpp | Implementation B - sorted vector levels + std::map orders |
| book_array.hpp | Implementation C - tick indexed array + cached best + vector of orders |
| bench.cpp | benchmark (`--impl A/B/C`) and correctness test (`--verify`) |
| run_perf.sh | runs verify, all benchmarks, perf stat and perf record, saves to results/ |
| results/ | the output I got on my machine |

## Build

```
g++ -std=c++17 -O2 -o bench bench.cpp
```

(run_perf.sh also adds `-g -fno-omit-frame-pointer` so perf record can show function names)

## Run

Correctness test:

```
./bench --verify
```

Benchmark (each one runs the workload 5 times and prints the median):

```
./bench --impl A
./bench --impl B
./bench --impl C
```

`--runs N` changes the number of runs. `--impl none` only builds the workload and is
used as a baseline for perf stat.

Everything (verify + bench + perf stat + perf record):

```
./run_perf.sh
```

## Interface changes

The public API is exactly the one given in the assignment (double prices, same 5 functions).
Only the inside is different:

- prices are converted to integer ticks (`llround(price * 100)`) as soon as they come in,
  and converted back to double only when returning BBO / Level. So no floating point compare
  is done anywhere inside the books.
- `add_order` ignores an order if the price is outside its band, qty <= 0, or the id is already live.
- Implementation C assumes order ids are positive and roughly sequential (like exchange
  sequence numbers) because it uses the id as an index into a vector. The benchmark ids are 1, 2, 3...

## Workload

- 1,000,000 operations, `std::mt19937 rng(42)`
- 70% add, 20% cancel, 10% get_bbo
- every 100 operations: get_bids(10) and get_asks(10)
- side 50/50, qty 1-100, price = random tick inside the side's band
- cancels always pick a random order that is still live
- the workload is generated once into a vector before timing, so all three books get the exact same ops
- each add / cancel / bbo is timed on its own with steady_clock, total time is the whole loop
- final BBO and a checksum (sum of quantities seen) are printed so nothing gets optimised away

## Data structures

### A - std::map + std::unordered_map

- Orders: `unordered_map<id, {side, tick, qty}>`
- Price levels: `map<tick, total qty>` for each side (bids use `greater<int>` so begin() is the best)
- Best bid / ask: `begin()` of each map
- Cancel: hash lookup of id, then `find` the tick in the map, subtract qty, erase level if it is 0

### B - sorted std::vector + std::map

- Orders: `map<id, {side, tick, qty}>` (red black tree instead of hash table)
- Price levels: `vector<{tick, qty}>` per side kept sorted, best price at the back
  (bids ascending, asks descending) so inserts/erases near the top move few elements
- Best bid / ask: `back()` of each vector
- Cancel: tree lookup of id, then binary search (`lower_bound`) for the tick in the vector

### C - my design: flat tick array + cached best + id vector

- Orders: `vector<{tick, qty}>` indexed directly by order id, qty 0 means empty slot
- Price levels: one array `long qty_at[15001]` indexed by tick. Bids and asks never overlap
  (bids 5000-9999, asks 10001-15000) so one array holds both. Only about 117 KB.
- Best bid / ask: two ints kept up to date. On add just compare with the new tick.
  On cancel, only if the best level becomes empty, walk the array to the next non empty level.
- Cancel: `orders[id]` gives the tick directly, subtract from `qty_at[tick]`

### Complexity

L = number of non empty price levels on a side (at most 5000), N = number of live orders,
d = depth asked for, R = ticks in one band (5000)

| Operation | A | B | C |
|-----------|---|---|---|
| add_order | O(log L) + O(1) avg hash | O(log N) + O(log L) search + O(L) worst shift | O(1) (amortised, vector growth) |
| cancel_order | O(1) avg hash + O(log L) | O(log N) + O(log L) + O(L) worst shift | O(1), O(R) worst when best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(d) | O(d) | O(d) when book is dense, O(R) worst |
| get_asks | O(d) | O(d) | O(d) when book is dense, O(R) worst |

## Correctness results

The same random workload is run through all three books and after **every** operation
the BBO and the top 10 levels on both sides are compared (prices and quantities).
Before that, each book also has to pass some hand written cases on its own: empty book,
cancel of unknown id, double cancel, same price orders adding up, asking for 10 levels when only 3 exist.

```
Running correctness test...

Edge case tests : A ok, B ok, C ok
Random workload : 1000000 ops, BBO + 10 levels each side compared after every op (1000000 checks)

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

All three also print the same final BBO (99.99 x 3244 / 100.01 x 2193) and checksum 522702630.

## Performance results

Machine: Intel i5-13450HX (hybrid, 6 P-cores + 4 E-cores), Linux 6.6, g++ 16.2, -O2.
All runs pinned to CPU 0 (a P-core) with `taskset -c 0` so the counters all come from the same core type.

Median of 5 runs (from results/bench_*.txt):

| | A (map) | B (vector) | C (array) |
|---|---|---|---|
| Total time (ms) | 165.57 | 218.83 | 52.09 |
| ns / add | 140.7 | 147.5 | 32.6 |
| ns / cancel | 238.5 | 472.0 | 51.9 |
| ns / bbo | 13.1 | 12.8 | 12.7 |

Full perf stat output is in results/perf_stat_A.txt, perf_stat_B.txt, perf_stat_C.txt
and perf record output in results/perf_report_A/B/C.txt. The completed results table is in REPORT.md.

perf stat for A:

```
 Performance counter stats for './bench --impl A --runs 1' (5 runs):

            218.99 msec task-clock                       ( +-  1.29% )
         998517949      cpu_core/cycles/                 ( +-  1.18% )
         899825586      cpu_core/instructions/           ( +-  0.06% )
         169530376      cpu_core/branches/               ( +-  0.07% )
           8816909      cpu_core/branch-misses/          ( +-  0.07% )
           3999900      cpu_core/cache-misses/           ( +-  5.43% )
          13542618      cpu_core/L1-dcache-load-misses/  ( +-  0.10% )
            646331      cpu_core/dTLB-load-misses/       ( +-  0.43% )
             15618      page-faults                      ( +-  0.80% )

       0.216287653 +- 0.003021506 seconds time elapsed  ( +-  1.40% )
```

perf stat for B:

```
 Performance counter stats for './bench --impl B --runs 1' (5 runs):

            280.57 msec task-clock                       ( +-  1.77% )
        1274439903      cpu_core/cycles/                 ( +-  1.79% )
        1315861276      cpu_core/instructions/           ( +-  0.28% )
         272617811      cpu_core/branches/               ( +-  0.27% )
          12426454      cpu_core/branch-misses/          ( +-  0.04% )
           4943415      cpu_core/cache-misses/           ( +-  6.46% )
          18605759      cpu_core/L1-dcache-load-misses/  ( +-  0.22% )
            902290      cpu_core/dTLB-load-misses/       ( +-  1.22% )
             14333      page-faults                      ( +-  5.46% )

       0.279499765 +- 0.005454338 seconds time elapsed  ( +-  1.95% )
```

perf stat for C:

```
 Performance counter stats for './bench --impl C --runs 1' (5 runs):

             78.68 msec task-clock                       ( +-  0.43% )
         355097302      cpu_core/cycles/                 ( +-  0.39% )
         508884910      cpu_core/instructions/           ( +-  0.02% )
          78804595      cpu_core/branches/               ( +-  0.02% )
           1592590      cpu_core/branch-misses/          ( +-  0.04% )
           1928641      cpu_core/cache-misses/           ( +-  1.35% )
           1592111      cpu_core/L1-dcache-load-misses/  ( +-  0.33% )
             37170      cpu_core/dTLB-load-misses/       ( +-  1.04% )
             12022      page-faults                      ( +-  0.00% )

       0.075135326 +- 0.000487720 seconds time elapsed  ( +-  0.65% )
```

Unavailable counters: since the process was pinned to a P-core, the `cpu_atom/*` (E-core) rows show
`<not counted>`, and `cpu_atom/L1-dcache-load-misses` is `<not supported>` on this CPU.
All `cpu_core/*` counters worked. I removed those empty cpu_atom rows above to keep it short,
they are still in the results/ files.
