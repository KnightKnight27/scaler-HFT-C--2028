# Low-latency order book

SST email: abhiroop.24bcs10287@sst.scaler.com  
Roll number: 24BCS10287

Three single-threaded resting order books for one instrument. Orders stay in
the book until cancelled; there is no matching or trade generation.

## Build and run

A C++17 compiler and the standard library are sufficient.

```sh
mkdir -p build
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic \
    -Iinclude src/bench.cpp tests/correctness.cpp -o build/bench

./build/bench --verify
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
```

Alternatively, use CMake:

```sh
cmake -S . -B build
cmake --build build
```

`--verify` runs the edge tests and a full million-operation comparison. For a
short check, use `--verify --ops 2000`. Benchmark defaults are one million
operations, seed 42 and five trials. `--help` lists the remaining options.

## API

All three books expose:

```cpp
void add_order(uint64_t id, Side side, int32_t price, int64_t qty);
bool cancel_order(uint64_t id);
BBO get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

Prices use integer ticks instead of the PDF's `double`, as permitted by the
assignment: `9999` means 99.99. This applies to input, `Level`, and `BBO`.
Quantities use `int64_t` instead of `long`. `Side` and the BBO presence flags
retain the specified behavior. An optional constructor argument provides an
expected live-order capacity hint, not a hard limit.

Bids range from 5000 to 9999 ticks; asks from 10001 to 15000. Each side has
5,000 possible price levels. Orders at the same price contribute to one total
quantity. Cancelling removes the whole order; unknown or already-cancelled IDs
return false. An empty side has a false BBO flag and zero price/quantity fields.

Depth queries return bids descending and asks ascending, with only the available
levels. Nonpositive depth returns an empty vector. Invalid side, price or
nonpositive quantity throws `std::invalid_argument`, as does a duplicate live ID.
IDs must be unique and never reused, but need not be consecutive or sorted.
Historical IDs are not retained to check reuse. Aggregate quantities must fit
in `int64_t`.

## Data structures

**A — MapBook:** an ID-to-order `std::map` and separate ordered maps for bid
and ask quantities. Cancellation looks up the order, updates its price level,
and removes an empty level. The beginning of each side's map provides BBO.

**B — VectorBook:** two sorted vectors of active price levels. `lower_bound`
finds a price; inserting or removing a level shifts later elements. The first
level is the best price. Orders occupy reusable vector slots, located through
an `std::unordered_map` from ID to slot. Orders store prices rather than level
positions because insertions can move levels.

**C — ArrayBook:** two fixed quantity arrays, indexed by `price - 5000` and
`price - 10001`. Orders live in a custom linear-probing hash table with full IDs
and empty/live/deleted states. Cancellation probes the table and subtracts from
the price slot. A cached BBO is updated on mutations; deleting the best level
scans toward worse prices. Depth queries scan and skip empty slots. The hash
table grows or rebuilds to keep occupied/deleted slots below roughly 70%.
Deleted slots do not terminate a lookup.

Let N be live orders, L active levels per side, P=5000 possible prices per side,
and k the number of returned levels:

| Operation | A | B | C |
|---|---|---|---|
| add_order | O(log N + log L) | Expected O(log L); O(L) for a new level | Expected amortized O(1) |
| cancel_order | O(log N + log L) | Expected O(log L); O(L) when removing a level | Expected O(1); up to O(P) to replace the best level |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(k) | O(k) | O(P) worst case |
| get_asks | O(k) | O(k) | O(P) worst case |

Hash lookups can degrade to O(N). Rehashing and vector growth can cost O(N);
C's worst-case cancellation is O(N+P). A uses O(N+L) storage. B retains order
and index capacity and reserves 5,000 levels per side. C uses O(table capacity+P)
storage; its price arrays hold 80,000 bytes. B/C retain capacity after deletion.
Both order storage and price storage differ between designs, so performance
changes cannot be attributed to the price container alone.

## Verification and measurement

Verification checks explicit expected results for empty/one-sided books,
aggregation, price boundaries, depth, repeated cancellation, sparse best-price
replacement and arbitrary IDs. It also checks all 10,000 levels and hash-table
collision, deletion, growth and churn cases. The random replay compares complete
bid/ask ladders, BBO, prices, quantities and cancellation results after every
operation. Failures return a nonzero exit status.

The workload uses `mt19937` seed 42, approximately 70% adds, 20% cancels and 10%
BBO queries. Sides are equally likely, prices are uniform valid ticks, and
quantities are 1–100. Cancels select live orders; if none exist, that operation
becomes an add. Rejection sampling makes the replay reproducible across standard
libraries. Both depth-10 queries run additionally every 100 operations.

Each of five trials uses fresh books. One replay measures total time; another
measures individual operations with `steady_clock`. Results are medians of five
trial totals and five per-trial operation averages. Generation and initial
reservation are outside timing; each design receives the same peak-live capacity
hint, although maps cannot reserve nodes. Total time includes loop dispatch,
checksums and depth-query allocations, but excludes per-operation clocks,
setup, destruction and printing. Individual timings include clock/dispatch
overhead. Empty clock intervals are reported without subtraction; the small
BBO differences are close to the timer's limits. Query results feed a printed
checksum, and the final BBO is printed. Verification is not timed.

## Linux profiling

Repeat these commands for A, B and C:

```sh
perf stat -r 5 \
    -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
    ./build/bench --impl A --runs 1 --total-only
perf record -g -- ./build/bench --impl A --runs 5 --total-only
perf report --stdio --percent-limit 1
```

`perf stat -r 5` runs five separate single-trial processes. Recording uses five
replays for more samples. If the default recording event is unavailable, use
`perf record -e cpu-clock -F 999 -g` with the same benchmark arguments.

The saved measurements used an Ubuntu 24.04 ARM64 Linux VM, GCC 13.3.0,
LinuxKit 7.0.12 and perf 6.8.12, without CPU pinning. Hardware cycles,
instructions, branches, branch misses, cache misses, L1 misses and dTLB misses
were unavailable. Task-clock, page faults and software task-clock call graphs
were recorded. Unavailable counters are marked N/A in the report.

Whole-process perf includes generation, setup, replay and teardown. Its repeated
statistics are retained as printed; chrono results use medians and measure the
replay only. The original recorded commands use `build/linux/bench`, the build
location used for those measurements.

## Deliverables

- `include/`, `src/`, `tests/`: the three books, shared benchmark and verification.
- `CMakeLists.txt`: build configuration.
- `results/correctness.txt`: full Linux correctness output.
- `results/benchmarks.txt`: five trials and medians for A/B/C.
- `results/perf-stat.txt`: raw counter output for A/B/C.
- `results/perf-report.txt`: call-graph reports for A/B/C.
- `REPORT.pdf`: two-page results table and performance analysis.
