# Build a Low-Latency Order Book

## 1. Project Overview

This project implements three different single-threaded order books in C++17.
The purpose is to compare how different data structures affect order-book
performance.

An order book stores buy and sell orders at different price levels. Multiple
orders at the same price are aggregated into one price level.

This project does not perform matching or generate trades. Orders remain in
the book until they are cancelled.

## 2. Implementations

### Implementation A: Ordered Map

File: `Implementation_A.cpp`

- Uses `std::map` to maintain price levels in sorted order.
- Uses `std::unordered_map` to track live orders.
- Uses `std::unordered_set` to prevent order IDs from being reused.
- Provides ordered access to the best bid and best ask.

### Implementation B: Sorted Vectors

File: `Implementation_B.cpp`

- Uses sorted vectors to store price levels.
- Maintains price ordering when levels are inserted or removed.
- Vector elements may need to shift during insertion and deletion.

### Implementation C: Fixed-Size Arrays

File: `Implementation_C.cpp`

- Uses fixed-size arrays to represent the supported price levels.
- Uses direct indexing to access a price level.
- Avoids dynamically inserting price levels into an ordered tree or sorted vector.

## 3. Price and Order Rules

- Programming language: C++17
- Instrument: One instrument
- Tick size: 0.01
- Valid bid prices: 5000 to 9999 ticks
- Valid ask prices: 10001 to 15000 ticks
- Tick 10000 is excluded.
- Orders with invalid prices or non-positive quantities are rejected.
- Order IDs must be unique and cannot be reused after cancellation.
- Orders at the same price are aggregated.
- Orders remain in the book until cancelled.
- No matching or trade execution is performed.

## 4. Main Operations

The implementations support the following operations:

- `add_order`: Adds a valid order to the book.
- `cancel_order`: Cancels an existing live order.
- `get_bbo`: Returns the best bid and best ask.
- `get_bids(depth=10)`: Returns the requested number of top bid levels.
- `get_asks(depth=10)`: Returns the requested number of top ask levels.

BBO stands for Best Bid and Offer. It represents the highest bid price and
the lowest ask price, along with their available quantities.

## 5. Compilation

Compile each implementation using an optimized C++17 build:

```bash
g++ -O2 -std=c++17 Implementation_A.cpp -o Implementation_A
g++ -O2 -std=c++17 Implementation_B.cpp -o Implementation_B
g++ -O2 -std=c++17 Implementation_C.cpp -o Implementation_C
```

## 6. Running the Benchmarks

Run each executable separately:

```bash
./Implementation_A
./Implementation_B
./Implementation_C
```

The benchmark uses a workload of 1,000,000 operations with a fixed random
seed of 12345. The approximate operation mix is:

- 70% add operations
- 20% cancel operations
- 10% BBO queries

Top-10 bid and ask queries are performed every 100 operations. The benchmark
uses `std::chrono::steady_clock`, runs five times, and reports the median
runtime.

## 7. Running Correctness Tests

Compile and run the tests for all three implementations:

```bash
g++ -O2 -std=c++17 test_order_book_A.cpp -o test_order_book_A
./test_order_book_A

g++ -O2 -std=c++17 test_order_book_B.cpp -o test_order_book_B
./test_order_book_B

g++ -O2 -std=c++17 test_order_book_C.cpp -o test_order_book_C
./test_order_book_C
```

The tests check order insertion, aggregation, invalid input rejection,
duplicate IDs, cancellation, and related order-book behaviour.

Expected result for each test is a message indicating that all tests passed.

## 8. Benchmark Results

The recorded median runtimes from the optimized benchmark were:

| Implementation | Median Runtime |
|---|---:|
| A - Ordered Map | 968.96 ms |
| B - Sorted Vectors | 3108.55 ms |
| C - Fixed-Size Arrays | 551.77 ms |

These results are specific to the tested machine, compiler, build settings,
and workload. Re-running the benchmark may produce different runtimes.

## 9. Performance Discussion

Implementation C was the fastest in the recorded benchmark. Fixed-size arrays
allow price levels to be accessed directly by index, reducing the work needed
to locate a price level.

Implementation A was faster than Implementation B in this workload. The
ordered map maintains sorted price levels, while sorted vectors may need to
shift elements when inserting or removing price levels.

Implementation B had the highest recorded runtime. The cost of maintaining
sorted vector elements can become significant when many price levels change.

The benchmark results suggest that direct indexing is effective for this
project's bounded price range. However, performance depends on the workload,
price range, memory usage, and implementation details.

All three implementations produced the same recorded benchmark checksum,
which is a useful consistency check but does not prove complete correctness.
The separate correctness tests provide additional validation.

## 10. Profiling Limitations

The `perf` tool was available, but hardware-counter collection and call-graph
recording were blocked by system performance-monitoring permissions.
Therefore, no reliable CPU-cycle, cache-miss, or function-level profiling
results are reported.

## 11. Conclusion

This project compares three approaches to implementing a low-latency order
book. Under the recorded benchmark workload, the fixed-size array design
performed best, followed by the ordered-map design and then the sorted-vector
design.

The experiment demonstrates how choosing an appropriate data structure can
significantly affect runtime performance.