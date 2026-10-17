# Order book

email: "guna.24bcs10070@sst.scaler.com"  
roll_no: "24BCS10070"

Three implementations of a single-instrument resting order book in C++17.
Orders can be added and cancelled; there is no matching. The book is
single-threaded and uses only the C++ standard library.

## Build and run

```bash
make
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
```

Each benchmark defaults to 1,000,000 operations, seed 42 and five runs. It
prints per-run timings, medians, the final BBO and a checksum.

```bash
./bench --impl C --runs 5 --csv results/example_C.csv
./bench --verify --ops 1000000 --seed 42
./bench --help
```

The full verifier is much slower than a benchmark: it compares every active
price level after every operation.

Without Make:

```bash
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic \
  bench.cpp workload.cpp verification.cpp implementation/map_order_book.cpp \
  implementation/vector_order_book.cpp implementation/array_order_book.cpp -o bench
```

CMake is also supported:

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

## The three designs

| | Order storage and ID lookup | Price levels | Best price |
| --- | --- | --- | --- |
| A | `std::unordered_map` | `std::map` per side | Map endpoints |
| B | `std::map` | Sorted vector per side | Vector endpoints |
| C | Custom flat hash table | Fixed arrays and occupancy bits | Cached indices |

A provides a straightforward baseline. B trades contiguous level storage for
vector shifts and tree-based cancellation lookup. C uses the fixed price range
to index levels directly; its order table uses linear probing and tombstones.
[Design notes](docs/design.md) cover the algorithms, invariants and complexity.

## API details

Prices use **integer ticks**, rather than the assignment's `double` interface:
`9950` means 99.50. The same change applies to `Level` and BBO price fields.
Valid buy ticks are 5000–9999; sell ticks are 10001–15000. Quantity is `long`.

The five methods are `add_order`, `cancel_order`, `get_bbo`, `get_bids`, and
`get_asks`. The query methods are const, and depth defaults to ten.

Adds require a valid side and price, positive quantity, never-used ID and totals
that fit in `long`. The hot path assumes these preconditions. Cancelled IDs
cannot be reused. Unknown or repeated cancellations return false. Empty BBO
sides have false presence flags and zero fields. Nonpositive depth returns
nothing; excessive depth returns available levels. Bids are descending and
asks ascending.

## Checks and measurements

`--verify` checks empty and one-sided books, cancellation, level aggregation,
depth limits, extreme IDs, all valid prices and hash-table turnover. The random
test compares all three books after each operation. A separate oracle rebuilds
levels from orders for the first 1,000 operations.

The full million-operation verification passed, as did ASan, UBSan and leak
checks. Saved output: [verification](results/verification_full.txt) and
[sanitizers](results/sanitizers.txt). Run the sanitizer suite with:

```bash
cmake -S . -B build-sanitize -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitize -j2
ctest --test-dir build-sanitize --output-on-failure
```

The [report](REPORT.md) includes measured results and analysis. Median replay
times on this machine were **164.507 ms (A), 215.782 ms (B), and 63.003 ms (C)**.
Individual timings include clock overhead; the BBO differences are especially
small. [Benchmark notes](docs/benchmarking.md) explain the two-pass timing,
workload generation, CPU affinity and perf commands.

Linux `perf` is needed only for profiling. There are no Python dependencies.

## Files

`implementation/` contains the three books; `workload.cpp` generates operations;
`verification.cpp` checks correctness; `bench.cpp` runs the selected book.
`results/` contains the recorded timings, correctness output and perf reports.
Build output is ignored by Git.

For the course submission, open a PR to `KnightKnight27/scaler-HFT-C--2028`.
