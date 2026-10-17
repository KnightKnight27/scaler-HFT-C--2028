# Low-Latency Order Book

This project implements a single-instrument resting order book in three different ways and compares their latency. There is no matching or trade generation. Quantities at the same price are aggregated.

Name :- Shrival Kumar

Email :- shrival.24bcs10254@sst.scaler.com

Roll no :- 24BCS10254

The implementations are intentionally kept in three separate source/header pairs:

- `include/implementation_a_map.hpp` + `src/implementation_a_map.cpp`
- `include/implementation_b_indexed.hpp` + `src/implementation_b_indexed.cpp`
- `include/implementation_c_array.hpp` + `src/implementation_c_array.cpp`

Only the common data types and price validation/conversion are shared through `include/order_book_types.hpp` and `src/price_utils.cpp`.

## Build and run

Requirements: CMake 3.16+ and a C++17 compiler. Release builds use `-O2`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the full one-million-operation agreement check:

```bash
./build/bench --verify
```

Run a benchmark (five runs, median reported):

```bash
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
```

`--ops N` and `--runs N` are available for quick experiments. The normal submission results use the default one million operations and five runs.

## Interface note

The public API keeps the required `double` price argument, but every implementation immediately validates and converts it to integer cents. A buy must be from 5000 through 9999 ticks, and a sell from 10001 through 15000 ticks. A non-cent price, invalid side range, non-positive quantity, or duplicate ID throws `std::invalid_argument`. Unknown and already-cancelled IDs return `false`.

## Three designs

### A — ordered levels, hashed orders

- Bid/ask levels: two `std::map<int, long>` containers in best-first order.
- Orders: `std::unordered_map<id, Order>`.
- Best price: first map element.
- Cancel: hash lookup followed by map update/erase.

### B — hashed quantities with ordered indexes

- Bid/ask quantities: two `std::unordered_map<int, long>` containers.
- Active prices: two `std::set<int>` containers in best-first order.
- Orders: `std::map<id, Order>`.
- Best price: first set element, then hash lookup for quantity.
- Cancel: tree lookup by ID, hash update, and possible set erase.

### C — fixed levels and a custom flat ID table

- Bid/ask quantities: one fixed array covering exactly 10,000 legal levels.
- Orders: custom open-addressed hash table with linear probing and tombstones.
- Best price: cached bid/ask array indices, repaired only when a best level empties.
- Cancel: flat hash lookup followed by direct array indexing.

`N` is the number of live orders, `P` active price levels, `D` returned depth, and `R` the bounded 5,000-level range on one side.

| Operation | A | B | C |
|---|---|---|---|
| `add_order` | average O(1) + O(log P) | O(log N) + average O(1) + O(log P) | average amortized O(1) |
| `cancel_order` | average O(1) + O(log P) | O(log N) + average O(1), sometimes O(log P) | average O(1), worst O(R) best repair |
| `get_bbo` | O(1) | average O(1) | O(1) |
| `get_bids(D)` | O(D) | average O(D) | O(R), stops after D levels |
| `get_asks(D)` | O(D) | average O(D) | O(R), stops after D levels |

## Correctness

The verifier sends the same fixed-seed workload to all books and compares BBO, the top ten bids, and the top ten asks after every operation. Separate tests cover empty books, aggregation, insufficient depth, unknown/repeated cancel, best-level removal, duplicate IDs, invalid ticks, ranges, and quantities.

```text
Running correctness test with 1000000 operations...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## Linux perf

Build the project on a native Linux machine with Linux `perf` installed, then run:

```bash
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,\
L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./build/bench --impl A

perf record -e cpu-clock -g ./build/bench --impl A
perf report
```

Repeat with B and C. The raw benchmark and `perf stat` text captures for all three implementations are in `results/`.

The comparison table is in [RESULTS.md](RESULTS.md).
