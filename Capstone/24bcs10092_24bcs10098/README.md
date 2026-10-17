# Low-Latency Order Book

**Daksh Shah** — 24bcs10092 — shah.24bcs10092@sst.scaler.com  
**Tirth Bhalani** — 24bcs10098 — tirth.24bcs10098@sst.scaler.com

A single-threaded C++17 order book with five operations: `add_order`, `cancel_order`, `get_bbo`, `get_bids` and `get_asks`. Orders are stored without matching or executing trades. Prices use integer ticks, and depth queries return aggregated quantities.

## Implementations

| Version | Data structure |
| --- | --- |
| [A](impl_a/) | `std::map` price levels with FIFO order lists |
| [B](impl_b/) | Indexed binary heaps with price-level quantity maps |
| [C](impl_c/) | Fixed tick arrays with occupancy bitmaps |

Bids use prices from 50.00 to 99.99; asks use 100.01 to 150.00. Each accepted order ID is unique for the lifetime of the book.

See [implementations.md](implementations.md) for design details and complexity.

## Build and run

From `Capstone/24bcs10092_24bcs10098` on Linux or WSL:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/demo
ctest --test-dir build --output-on-failure
```

The build uses C++17 and `-O2`. The demo runs the same example on all three implementations.

## Tests

The verifier has 78 deterministic cases. It also supports randomized checks against a separate reference model:

```bash
./build/verify
./build/verify --random --ops 100000 --seed 42
```

The 78 deterministic cases and 100,000-operation randomized checks passed for seeds 42, 7 and 2026.

## Benchmark

```bash
./build/bench --impl A --ops 1000000 --runs 5 --seed 42
./build/bench --impl B --ops 1000000 --runs 5 --seed 42
./build/bench --impl C --ops 1000000 --runs 5 --seed 42
```

Each run uses 700,000 adds, 200,000 live-order cancellations, 100,000 BBO queries, and bid/ask depth queries every 100 operations.

| Implementation | Median of 5 runs |
| --- | ---: |
| A | 429.377 ms |
| B | 6737.518 ms |
| C | 327.892 ms |

Measured on Ubuntu WSL2 with GCC 15.2.0. C was fastest on this workload. B spent most of its time sorting price levels for depth queries.

Full results: [benchmark.md](benchmark.md) · [profiling.md](profiling.md) · [analysis.md](analysis.md).

## Profiling

```bash
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer"
cmake --build build-profile
sh scripts/profile.sh
```

The script collects `perf stat` counters and call graphs for A, B and C. Text results are in [profiling/](profiling/).

## Screenshots

The [screenshots folder](screenshots/) contains six terminal captures: verification, demo, perf counters for A/B/C, and B's hotspot report. Screenshot coverage is partial; benchmark and some profiling output is documented as text instead.
