## Submission Details

- **SST Email:** ardhi.24bcs10557@sst.scaler.com
- **Roll Number:** 24BCS10557
- **Name:** Ardhi Jasmine


# C++ Order Book Implementations

## Overview

This project implements a single-instrument, single-threaded limit order book
in standard C++. It supports adding orders, cancelling orders, querying the
best bid and offer (BBO), and retrieving bid/ask depth.

The book does not match orders or execute trades. Orders remain in the book
until cancelled.

## Price and order rules

- Tick size: 0.01.
- Bid prices: 50.00 through 99.99.
- Ask prices: 100.01 through 150.00.
- Mid-price: 100.00.
- Prices must lie on valid ticks.
- Multiple orders can share a price level; level quantity is their sum.
- Order IDs are unique and cannot be reused.
- Cancelling an unknown or already-cancelled order returns false.
- An empty book reports no bid and no ask.
- Depth queries return up to the requested number of available price levels.

Prices are represented internally using integer ticks to avoid floating-point
rounding issues.

## Implementations

### A — Ordered map

Uses ordered maps to keep price levels sorted, with order-ID lookup for
cancellation. This is a straightforward design with ordered price traversal.

### B — Priority heaps

Uses a max-heap for bids and a min-heap for asks, alongside hash maps for
orders and price-level quantities. Heap entries can become stale after
cancellation, so queries must skip stale entries.

### C — Fixed arrays

Uses arrays indexed by integer price ticks for bid and ask level quantities,
plus hash-based order lookup. The fixed, bounded price range allows direct
access to a price level without a tree or heap traversal.

## Time Complexity Analysis

Let `N` be the number of stored orders, `P` the number of active price levels, `D` the requested depth, `H` the number of entries in a heap (including stale entries), and `K = 5000` the number of possible price ticks per side in Implementation C.

| Operation | A: Ordered Maps | B: Heaps + Hash Maps | C: Fixed Arrays |
|---|---|---|---|
| Add order | O(log P) | Expected O(1); O(log H) when adding a new heap entry | Expected O(1) |
| Cancel order | O(log P) | Expected O(1), excluding deferred heap cleanup | Expected O(1) |
| Get BBO | O(1) | Usually O(1); stale entries may require cleanup | O(K) worst case |
| Get top D levels | O(min(D, P)) | O(H + D log H) as a typical upper bound when extracting D valid levels; stale entries can add work | O(K) worst case |

**Implementation A:** Ordered maps maintain price levels in sorted order. The best bid and ask can be accessed at the beginning of their respective maps.

**Implementation B:** Priority queues provide quick access to the best price. Cancelled price levels can leave stale heap entries, which are removed when the best price is queried. Depth queries copy the heap before extracting levels, adding overhead.

**Implementation C:** Fixed arrays allow direct access to price-level quantities. Finding the best price or collecting depth requires scanning the relevant price range. The scan is bounded by the fixed tick range, regardless of the number of orders.

Hash-map operations in B and C have expected O(1) complexity, rather than a strict worst-case O(1) guarantee. The complexity of BBO and depth queries in Implementation B can also vary with the number of stale heap entries.


## Build

Run these commands from the repository root.

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -ICapstone/order_book/include \
    Capstone/order_book/src/implementation_a.cpp \
    Capstone/order_book/tests/test_a.cpp \
    -o Capstone/order_book/test_a

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -ICapstone/order_book/include \
    Capstone/order_book/src/implementation_b.cpp \
    Capstone/order_book/tests/test_b.cpp \
    -o Capstone/order_book/test_b

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -ICapstone/order_book/include \
    Capstone/order_book/src/implementation_c.cpp \
    Capstone/order_book/tests/test_c.cpp \
    -o Capstone/order_book/test_c
```

## Run the tests

```bash
./Capstone/order_book/test_a
./Capstone/order_book/test_b
./Capstone/order_book/test_c
```

Build and run the cross-implementation test:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -ICapstone/order_book/include \
    Capstone/order_book/src/implementation_a.cpp \
    Capstone/order_book/src/implementation_b.cpp \
    Capstone/order_book/src/implementation_c.cpp \
    Capstone/order_book/tests/test_all.cpp \
    -o Capstone/order_book/test_all

./Capstone/order_book/test_all
```

The cross-implementation test compares results over 20,000 reproducible
operations, including BBO and top-10 bid/ask depth.

## Benchmark

Build the benchmark executable:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -ICapstone/order_book/include \
    Capstone/order_book/src/implementation_a.cpp \
    Capstone/order_book/src/implementation_b.cpp \
    Capstone/order_book/src/implementation_c.cpp \
    Capstone/order_book/tests/benchmark.cpp \
    -o Capstone/order_book/benchmark
```

Run each implementation:

```bash
./Capstone/order_book/benchmark --impl A
./Capstone/order_book/benchmark --impl B
./Capstone/order_book/benchmark --impl C
```

The benchmark uses a fixed seed and one million operations per run, with
approximately 70% adds, 20% cancellations, and 10% BBO queries. It reports
five runs, their median total time, average operation latencies, and a checksum.

## Hardware profiling

Example hardware-counter command:

```bash
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./Capstone/order_book/benchmark --impl A
```

Replace `A` with `B` or `C` to profile the other implementations.

Record call stacks with:

```bash
perf record -g -o Capstone/order_book/profile_A.data \
  ./Capstone/order_book/benchmark --impl A
perf report -i Capstone/order_book/profile_A.data
```

## Results and analysis

See `performance_analysis.md` for the measured benchmark results, hardware
counter observations, and design trade-offs.
