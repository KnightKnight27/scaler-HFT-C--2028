# Low-Latency Order Book

Three single-threaded C++17 order books for one instrument, compared with the same deterministic workload.

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The build uses the C++ standard library and `-O2`.

## Run

```bash
./build/bench --verify
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
```

The benchmark uses 1,000,000 operations, seed `42`, approximately 70% adds, 20% live cancellations, and 10% BBO queries. Bid and ask depth 10 are queried every 100 operations. Each benchmark command runs five times and reports the median.

## Interface

Prices are represented as integer ticks instead of `double`: `100.01` is tick `10001`. This avoids floating-point equality issues. Output prices are printed as ticks; the valid ranges are bids `5000..9999` and asks `10001..15000`.

## Implementations

- **A — map:** `std::map` price levels plus an unordered order-ID lookup.
- **B — lazy heap:** authoritative maps plus bid/ask heaps and generation-checked stale entries.
- **C — array:** two contiguous 5,000-entry price arrays plus maintained best ticks.

All three support add, cancel, BBO, and depth through the same logical interface. There is no matching engine or trade generation.

## Profiling

On a Linux machine with `perf`:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./build/bench --impl A
perf record -g ./build/bench --impl A
perf report
```

Repeat for B and C. Record unavailable counters instead of substituting unsupported values.

## Reports

- `reports/correctness-results.md`
- `reports/benchmark-results.md`
- `reports/performance-analysis.md`
- `reports/project-brief.md`
