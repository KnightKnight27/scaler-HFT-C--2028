# Order Book Project Context

## Domain

- **Instrument** - the one market instrument represented by this project.
- **Order** - a resting buy or sell instruction identified by a unique ID.
- **Side** - `Buy` or `Sell`.
- **Price tick** - an integer representation of a price at 0.01 resolution; `100.01` is tick `10001`.
- **Price level** - one tick on one side of the book and the sum of quantities resting there.
- **Bid** - a buy-side price level; the best bid is the highest bid tick.
- **Ask** - a sell-side price level; the best ask is the lowest ask tick.
- **BBO** - the best bid and best ask, including whether either side exists.
- **Depth** - the requested number of price levels returned from one side.
- **Live order** - an order that has been added and not cancelled.
- **Workload** - the fixed-seed sequence of add, cancel, BBO, and depth operations used for verification and benchmarking.

## Bounded contexts

- **Order-book context** - orders, price levels, cancellation, BBO, and depth queries.
- **Benchmark context** - workload generation, timing, checksum output, and median reporting.
- **Analysis context** - `perf` counters, results tables, and explanations of latency and memory behavior.

## Decisions

- Prices are stored internally as integer ticks to avoid floating-point equality problems.
- All three implementations expose the same logical API but may use tick prices internally.
- The book is resting-only: there is no matching, trade generation, or multi-instrument routing.
