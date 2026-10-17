# Low-latency order book

Archit Kulkarni  
Roll number: 24BCS10194  
SST email: archti.24bcs10194@sst.scaler.com

Single instrument, single thread, no matching. Uses C++17 and the standard library.

## Build and run

From this directory on Linux/WSL:

```sh
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic bench.cpp -o bench
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
```

On Windows with MinGW, use the same command with `-o bench.exe`, then `./bench.exe`.
Linux `perf` needs Linux/WSL. No CMake or external library is needed.

The default benchmark runs 1,000,000 operations five times and prints the median
of each metric. Seed 42 gives about 70% adds, 20% live cancels and 10% BBO queries.
Both depth APIs are also called every 100 operations. Each repeat starts empty.
Workload generation, book construction/destruction and the final full-state
checksum are outside the reported total. The total includes the operation
timers, checksum handling and the periodic depth queries.

Individual API times use `steady_clock` around each call. Timer overhead is
included, so small BBO differences should be treated cautiously. `--profile`
removes the per-call timers for profiling; its timings are not the report's
latency measurements. Every query result and the final full book feed a checksum.

```sh
./bench --impl C --runs 5 --ops 1000000 --seed 42 --csv results.csv
./bench --verify --ops 1000000  # full comparison after every operation; slower
```

The default verifier compares all price levels and the BBO after every operation
of a 20,000-operation workload, using the same generator as the benchmark. It
also checks empty/one-sided books, price boundaries, aggregation, sorting,
limited/oversized depth, unknown/repeated cancels, removal of the best level,
nonsequential 64-bit IDs, slot reuse and rejected inputs. Failures exit nonzero.

## API

All three classes in `order_book.hpp` expose `add_order`, `cancel_order`,
`get_bbo`, `get_bids` and `get_asks`. Prices in the API, `Level` and `BBO` are
integer cents (`9999` means `99.99`). Quantities are `int64_t` rather than `long`
so they are 64-bit on Windows too. Printed prices use decimal notation.

Bid levels are descending; ask levels are ascending. Nonpositive depth returns
an empty vector; oversized depth returns the available levels. Missing BBO
sides have a false flag and zero price/quantity. Invalid prices/quantities and
duplicate live IDs throw `invalid_argument`. IDs must never be reused by the
caller, and aggregate quantities must fit in `int64_t`.

## Designs and complexity

A (`MapBook`) keeps live orders in a hash table and quantities in two price
trees. Best prices come from the trees' endpoints. Cancellation hashes the ID,
then finds and updates its level in the corresponding tree.

B (`VectorBook`) keeps live orders in an ordered ID tree and price levels in
sorted vectors. Best prices come from the vectors' endpoints. Cancellation
looks up the ID in the tree and binary-searches its price vector. Adding or
removing a price level can shift the rest of the vector.

C (`ArrayBook`) uses two fixed arrays of 5,000 quantities and cached best
indices. It stores orders in vector slots with a hash table from ID to slot.
Cancelled slots go on a free list for reuse. Price updates index the arrays
directly; removing the best level scans to the next occupied slot.

Let N be live orders, L occupied levels on a side, P = 5,000 possible levels on
a side, and k = min(requested positive depth, L).

| Operation | A | B | C |
| --- | --- | --- | --- |
| add_order | expected amortized O(log L) | O(log N + log L), O(log N + L) if inserting a level | expected amortized O(1) |
| cancel_order | expected O(log L) | O(log N + log L), O(log N + L) if removing a level | expected amortized O(1), O(P) when scanning after best removal |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(k) | O(k) | O(P) worst case |
| get_asks | O(k) | O(k) | O(P) worst case |

Hash tables can degrade to O(N) per lookup/update; vector growth and hash
rehashing can also cause allocation spikes. Live data takes O(N + L) for A/B;
A's hash buckets and B's level vectors can retain peak capacity. C uses
O(N_peak + P) space, including its recycled slots and retained vector capacity.

## Results and perf

Measured results and the short analysis are in `REPORT.md`. Raw output is under
`results/`. To regenerate everything (requires installed `g++` and `perf`):

```sh
bash run_results.sh
# If perf permissions require it: sudo bash run_results.sh
```

The script tries all requested counters, records unavailable ones, and retries
with working counters. It also saves text reports from `perf record -g` and
falls back to the software `cpu-clock` sampler when hardware sampling fails.
`perf stat -r 5` reports means and variability; the latency table uses the
separate benchmark's five-run medians. Counters cover the whole process,
including generation and teardown, with five replays per process.
