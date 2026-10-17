# Order Book Capstone

Email: piyush.24bcs10234@sst.scaler.com  
Roll no: 24bcs10234

This project compares three ways to store an order book for one instrument. Each version supports adding an order, cancelling by id, finding the best bid and ask, and returning the top price levels. Quantities at the same price are added together.

This is a book of resting orders. It does not match trades or maintain a FIFO queue of orders at each price. The accepted bid and ask price bands do not overlap.

The measurements and their limitations are in [REPORT.md](REPORT.md). The original output files are in `results/`.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/bench --verify
./build/bench --impl all
```

Or build directly:

```bash
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra src/*.cpp -o bench
```

The code uses C++17. The optimisation flag is `-O2`; the other flags keep debugging information for profiling.

To run one version:

```bash
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
```

The optional arguments are `--runs N`, `--ops N` and `--seed N`. Their defaults are 5 runs, 1,000,000 operations and seed 42.

## Implementations

| Version | Price levels | Orders indexed by id |
|---|---|---|
| A: `MapOrderBook` | Two `std::map`s | `std::unordered_map` |
| B: `SortedVectorOrderBook` | Two sorted vectors | A vector sorted by id |
| C: `ArrayOrderBook` | An array and a bitmap | A custom hash table |

**A:** Bids are sorted from high to low and asks from low to high. The first entry gives the best price. Cancelling an order needs a hash lookup followed by a map lookup. This is the easiest version to follow, but it allocates a separate node for each order.

**B:** Binary search finds an order or price level. The best price is at the back of each level vector. A cancelled order is marked with quantity 0; when more than half the order entries are cancelled, they are removed in one pass. The storage is contiguous, but inserting a level can shift elements and a cancel needs two searches.

**C:** Each price tick maps directly to an array index. A bitmap records which levels are occupied, and two saved indexes give the best bid and ask. The custom hash table uses linear probing and moves entries back after an erase so that searches still work. It also aligns storage to 64 bytes and groups consecutive ids in the same cache line. This is the most involved version; its design and measurements are discussed in the report.

Let N be the number of live orders, L the number of occupied levels on one side, and d the requested depth:

| Operation | A | B | C |
|---|---|---|---|
| Add | O(log L), average hash cost | O(log L) for an existing level and increasing id; O(L) for a new level; O(N) for an out-of-order id | O(1) expected, excluding table growth |
| Cancel | O(log L), average hash cost | O(log N + log L), with O(L) level removal or occasional O(N) compaction | O(1) expected, plus a bounded bitmap scan if the best level empties |
| Best bid and ask | O(1) | O(1) | O(1) |
| Depth | O(d) | O(d) | O(d + W), where W is at most 157 bitmap words |

These are summaries for this fixed price range. Hash lookups can take longer when many ids collide, and container growth can cause a slower individual operation.

## Prices and input rules

The public methods take and return `double` prices. Internally, every version uses integer ticks: 99.50 is stored as 9950. This avoids using floating-point values as price keys. Changing the public price type would also require changing the callers and tests.

- Bid prices: 50.00 to 99.99. Ask prices: 100.01 to 150.00.
- Prices must be multiples of 0.01, allowing a small conversion tolerance. NaN and infinity are rejected.
- An order quantity must be between 1 and 2,147,483,647. Level totals use `long`.
- `add_order` returns `false` for invalid input or an id that is already live.
- `cancel_order` returns `false` if the id is not live. A cancelled id can be used again.
- Depth 0 or below returns an empty vector.

Each constructor accepts an optional `expected_orders` capacity hint. The benchmark supplies the maximum live-order count from its generated workload. Without the hint, containers grow as needed. `src/order_book.hpp` aliases `OrderBook` to version C.

## Correctness checks

`--verify` checks empty books, repeated cancels, depth order, input limits, unusual ids and all valid price levels. It also compares the custom hash table with `std::unordered_map` over 400,000 random operations.

The random book checks run A, B, C and a simple reference implementation together. They compare return values, the BBO and the top 10 levels after every operation, and all levels every 10,000 operations. There is also a workload with frequent cancels to exercise levels becoming empty. Saved output is in `results/macos/verify.txt` and `results/linux-docker/verify.txt`.

## Benchmark method

The workload is generated before timing using `std::mt19937` with a fixed seed: about 70% adds, 20% cancels and 10% BBO queries. Every 100 operations there is a depth query for each side. Cancels choose live orders. The code maps random draws to ranges itself so that the workload is the same with libc++ and libstdc++.

Each run uses two fresh books. The first pass times the whole workload. The second times individual calls and subtracts the measured clock overhead. A separate loop times 1,000,000 BBO calls because a single call is too short to measure reliably. Each pass calculates a checksum from its results; the two passes must agree.

`--impl all` alternates A, B and C and reports median measurements. The capacity hint avoids growth during these runs, so the results do not measure the cost of resizing an undersized book.

## Profiling and files

For a Linux profiling pass without timing each call:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./build/bench --impl A --perf
perf record -g ./build/bench --impl A --perf
perf report
```

`--impl none --perf` generates only the workload, providing a baseline. Docker Desktop did not expose hardware counters for the saved Linux runs. The report distinguishes its software counters from the macOS hardware counts and cachegrind simulations.

| Files | Purpose |
|---|---|
| `src/common.hpp` | Shared types, price conversion and validation |
| `src/*order_book.*`, `src/order_table.hpp` | The three books and C's hash table |
| `src/workload.*`, `src/verify.*` | Workload generation and correctness checks |
| `src/bench.cpp`, `src/platform.*` | Timing, output and OS counters |
| `scripts/run_all.sh` | Build, verify and save benchmarks |
| `scripts/perf_linux.sh`, `scripts/perf_docker.sh` | Profiling on Linux or in Docker |
| `scripts/experiment_scaling.sh` | Benchmark different workload sizes |

The books use standard-library containers and GCC/Clang bit-count built-ins. The profiling helpers also use OS-specific calls. These are portability limits if building with another compiler or operating system.
