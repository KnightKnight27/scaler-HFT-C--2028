# Order book capstone

Archit Kulkarni | 24BCS10194 | archti.24bcs10194@sst.scaler.com

Single instrument, single thread, no matching. C++17 and the standard library.

## Build and run

```sh
bash run_results.sh  # build, verify, benchmark and save perf output
```

Or build and run each part from this directory:

```sh
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic bench.cpp -o bench
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
```

Profiling needs Linux/WSL and `perf`; use `sudo bash run_results.sh` if its
permissions require it. On Windows/MinGW, build with `-o bench.exe` and run
`./bench.exe`; profiling still needs Linux.

The default verifier compares the BBO and every price level after each of
20,000 random operations. It also checks empty books, aggregation, depth,
unknown/repeated cancels, price boundaries, invalid adds and slot reuse.
Use `./bench --verify --ops 1000000` for a longer check. Failures exit nonzero.

## API and designs

All three books expose the five assignment operations. Prices are integer
cents (`9999` = `99.99`); quantities are `int64_t`. Invalid adds and duplicate
live IDs throw `invalid_argument`. IDs must never be reused, and totals must
fit in `int64_t`. Missing BBO sides have false flags and zero values.
Bids return highest price first, asks lowest first. Nonpositive depth returns
nothing; oversized depth returns the available levels.

| Book | Price storage | Orders and cancellation | Best price |
| --- | --- | --- | --- |
| A: MapBook | two maps | hash table: ID to order, then find its price in the map | map endpoints |
| B: VectorBook | two sorted vectors | ordered ID map, then binary search the price vector | vector endpoints |
| C: ArrayBook | two arrays of 5,000 quantities | hash table: ID to a reusable vector slot | cached indices; scan when best level empties |

N = live orders, L = occupied levels per side, P = 5,000, k = returned levels.

| Operation | A | B | C |
| --- | --- | --- | --- |
| add_order | O(log L) expected amortized | O(log N + log L); O(log N + L) for a new level | O(1) expected amortized |
| cancel_order | O(log L) expected | O(log N + log L); O(log N + L) if a level empties | O(1) expected amortized; O(P) best-level scan |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(k) | O(k) | O(P) worst case |
| get_asks | O(k) | O(k) | O(P) worst case |

Hash lookups can degrade to O(N). Growth and rehashing can cause allocation
spikes. Live data takes O(N + L) for A/B; container capacity can remain at its
peak. C uses O(N_peak + P) space, including recycled slots.

## Measurements

The benchmark uses 1,000,000 operations, seed 42, about 70% adds / 20% live
cancels / 10% BBO queries, plus both depth-10 queries every 100 operations.
It reports five-run medians, starting with an empty book each time.
`--ops`, `--seed`, `--runs` and `--csv` override the defaults or save CSV output.

Per-call times include clock overhead. Total time includes these timers,
depth queries and checksum handling; it excludes workload generation, book
construction/destruction and the final full-state checksum. Every query result
is consumed. `--profile` disables per-call timers for perf. Counter results are
whole-process means over five processes, with five book replays per process.

The measured table and analysis are in [REPORT.md](REPORT.md). Raw measurements,
verification and sanitizer output are in [results/](results/).
