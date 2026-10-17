# Low-Latency Order Book Capstone

**Student:** Ujjawal Prabhat  
**SST email:** ujjawal.24bcs10267@sst.scaler.com  
**Roll number:** 24BCS10267

This is a single-instrument, resting order book. It does not match orders or
generate trades. Prices are stored as integer cents internally, so `100.01`
is tick `10001`; this avoids floating-point equality errors while retaining
the brief's decimal API.

## Build and run

```sh
cd Capstone/order_book
make
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
```

`--verify` runs edge cases and replays the same deterministic one-million
operation workload through all three implementations, comparing BBO and
top-10 depth after every operation. The benchmark uses seed `42`, 70% adds,
20% valid live-order cancellations, 10% BBO queries, and depth queries every
100 operations. Each benchmark is run five times and reports medians.

## Implementations

| Design | Orders | Price levels | Best price | Add | Cancel | BBO |
|---|---|---|---|---:|---:|---:|
| A | `unordered_map<id, Order>` | ordered `std::map` | `begin()` | O(log L) | O(log L) | O(1) |
| B | `unordered_map<id, Order>` | hash maps plus max/min heaps | heap top with lazy cleanup | O(log L) | O(1) average | O(1) amortized |
| C | `unordered_map<id, Order>` | fixed array indexed by tick | cached best tick | O(1) | O(5000) worst case when a level empties | O(1) |

`L` is the number of occupied price levels. A depth query is O(depth) in the
number of returned levels. B copies and filters a heap, so its depth query is
O(H) where `H` is the number of heap entries. C may scan its bounded 5,000-tick
side range to skip empty levels. Unknown or already-cancelled IDs return
`false`. Duplicate IDs and invalid prices/quantities are rejected with an
exception.

## Profiling

Linux submissions can collect the requested counters with:

```sh
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl B
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,\
cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl C
perf record -g ./bench --impl A && perf report
```

This project was developed on macOS, where Linux `perf` hardware counters are
not available. Do not invent counter values: use Instruments' Time Profiler or
`sample` for a macOS profile, and report the platform substitution and its
limitations. The benchmark's `std::chrono` measurements remain portable.

## Submission

All source, documentation, and generated benchmark output belong in this
directory. The assignment requires submission through a pull request only.
