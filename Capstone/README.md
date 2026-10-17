# Low-Latency Order Book

## Overview

This project implements three single-threaded order books in C++17 using
only the standard library. Each implementation supports adding orders,
cancelling orders by ID, querying the best bid and offer (BBO), and
retrieving aggregated market depth.

## Requirements

- C++17 or later; standard library only.
- One instrument with tick size 0.01.
- Bid prices: 50.00–99.99.
- Ask prices: 100.01–150.00.
- Midpoint: 100.00.
- Prices are represented internally as integer ticks.
- Quantities must be positive.
- Order IDs are unique and cannot be reused after cancellation.
- Cancelling an unknown or already-cancelled order returns false.
- Depth queries return up to the requested number of occupied price levels.
- No order matching or trade execution.

## Design A: Ordered Maps and Lists

File: `order_book_a.cpp`

- Ordered maps maintain bid and ask price levels.
- Linked lists store orders at each price in insertion order.
- A hash map locates orders for cancellation.
- A set prevents order ID reuse.
- The best price is available at the beginning of the ordered map.

Trade-off: Efficient best-price and ordered-depth access, but tree operations
and allocations add overhead.

## Design B: Priority Queues

File: `order_book_b.cpp`

- A max-heap maintains bids and a min-heap maintains asks.
- A hash map tracks active orders.
- Aggregate quantities are maintained by price.
- Lazy deletion removes stale heap entries when encountered.
- Depth queries operate on copies of the heaps.

Trade-off: Fast BBO queries, but copying heaps for depth queries increases
total runtime.

## Design C: Fixed Price-Level Array

File: `order_book_c.cpp`

- Fixed arrays represent the supported price range.
- Each price level stores its orders and aggregate quantity.
- A hash map locates orders for cancellation.
- A set prevents order ID reuse.
- Best-price and depth queries scan the price levels.

Trade-off: Fast updates through direct indexing, but price-level scans make
BBO queries more expensive.

## Build and Test

Run from the repository root:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    Capstone/order_book_a.cpp Capstone/test_a.cpp -o Capstone/test_a

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    Capstone/order_book_b.cpp Capstone/test_b.cpp -o Capstone/test_b

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    Capstone/order_book_c.cpp Capstone/test_c.cpp -o Capstone/test_c

g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
    Capstone/order_book_a.cpp Capstone/order_book_b.cpp \
    Capstone/order_book_c.cpp Capstone/verify.cpp -o Capstone/verify

g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
    Capstone/order_book_a.cpp Capstone/order_book_b.cpp \
    Capstone/order_book_c.cpp Capstone/bench.cpp -o Capstone/bench



## Benchmark Results

Five runs were collected for each implementation. The table reports the
median of the five runs on the test machine.

| Metric | Design A: Map | Design B: Heap | Design C: Array |
|---|---:|---:|---:|
| Median total runtime | 765.96 ms | 12167.47 ms | 942.33 ms |
| Median average add latency | 625 ns | 618 ns | 308 ns |
| Median average cancel latency | 806 ns | 791 ns | 542 ns |
| Median average BBO latency | 340 ns | 106 ns | 5181 ns |
| Operations per run | 1,000,000 | 1,000,000 | 1,000,000 |
| Final checksum | 256162273181 | 256162273181 | 256162273181 |

The latency figures are the medians of the five reported per-operation
averages. Total runtime includes workload generation and periodic depth
queries, so it is not the sum of the three latency averages.

Raw results from all 15 runs are saved in `benchmark_runs.txt`.

### Analysis

Design A achieved the lowest total runtime in this workload. Its ordered
maps provide efficient access to the best price and ordered depth.

Design B achieved the lowest BBO latency, but copying priority queues for
depth queries significantly increased total runtime.

Design C achieved the lowest add and cancellation latencies. Direct indexing
reduces update overhead, but scanning price levels makes BBO queries slower.

All three implementations produced the same final BBO and checksum:

- Final BBO: bid 99.99, quantity 2609; ask 100.01, quantity 2652.
- Checksum: `256162273181`.

Results depend on the workload, compiler configuration, implementation,
and test machine.

## Hardware Performance Counters

Linux `perf stat` was attempted for designs A, B, and C using five
repetitions and events including cycles, instructions, branches,
branch misses, cache misses, L1 data-cache misses, dTLB misses, and page
faults.

Measurements could not be collected because the environment restricts
performance monitoring. It reported "No supported events found" and
`kernel.perf_event_paranoid = 4`.

Hardware-counter values are unavailable and have not been fabricated.
The comparison therefore uses measured wall-clock benchmark timings.
