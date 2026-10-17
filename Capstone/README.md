# Low-Latency Order Book

| Student Name     | Roll No    |
|------------------|------------|
| Narendra Sirvi   | 24bcs10225 |
| Ojas Maheshwari  | 24bcs10227 |

A C++20 order book for a single instrument. It does not match orders: buy and sell orders stay in the book until they are cancelled. Prices use a tick size of `0.01`; bids range from `50.00` to `99.99` and asks from `100.01` to `150.00`.

We built the order book three ways:

- **Implementation A** keeps bid and ask price levels ordered in a `std::map`.
- **Implementation B** stores price levels in priority queues and uses hash maps for lookup. It uses lazy deletion: empty levels are discarded when the heap is queried.
- **Implementation C** keeps orders in sorted `std::list`s. Each order stores its list iterator, so cancelling is fast, but adding an order means searching the list for its position.

All three implement the `IOrderBook` interface in [order.h](order.h). The data structures are described in more detail in [implementations.md](implementations.md).

## Interface changes

The assignment allows the interface to be changed as long as the changes are documented. We made two changes, both for readability.

### `BBO` holds two `Level`s

The assignment's `BBO` lists six separate fields. Ours groups each side's price and quantity into the `Level` struct that `get_bids()` and `get_asks()` already return:

<table>
<tr><th>Assignment</th><th>Our code (<a href="order.h">order.h</a>)</th></tr>
<tr><td>

```cpp
struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;
};
```

</td><td>

```cpp
struct BBO {
    Level bid_level{};
    Level ask_level{};
    bool has_bid{false};
    bool has_ask{false};
};
```

</td></tr>
</table>

| Assignment field | Our field |
|---|---|
| `bid_price` | `bid_level.price` |
| `bid_qty` | `bid_level.qty` |
| `ask_price` | `ask_level.price` |
| `ask_qty` | `ask_level.qty` |
| `has_bid`, `has_ask` | unchanged |

We changed it for readability:

- **One type for a price level.** The best bid is a `Level`, the same as each entry in `get_bids()`, so code that prints or compares levels works for both.
- **Price and quantity stay together.** `bbo.bid_level` is a single value you can pass around, so the bid's price can't be paired with the ask's quantity by mistake.
- **Safe defaults.** Every field has a default value, so a `BBO{}` already means an empty book (`has_bid == false`, `has_ask == false`) and no field is left uninitialised.

### A shared `IOrderBook` base class

The five required operations are declared as pure virtual functions in `IOrderBook`. `OrderBookA`, `OrderBookB` and `OrderBookC` all inherit from it, so the benchmark and the correctness test run every implementation through the same code. [orderbook.h](orderbook.h) defines `OrderBook` as an alias for `OrderBookA`, so the class name the assignment uses still works.

The function signatures match the assignment. Prices are still passed as `double`. Each implementation rounds them to integer ticks with `price_to_ticks()` and converts back with `ticks_to_price()` (both in [order.h](order.h)), so floating-point error can't produce two levels for the same price.

## Performance report

The full results and discussion are in [benchmark.md](benchmark.md):

- [Benchmark results](benchmark.md#benchmark-results): median timings, page faults and peak memory for A, B and C
- [`perf stat` results](benchmark.md#perf-stat-results): cycles, instructions, IPC, branch misses, cache misses and page faults
- [Analysis](benchmark.md#analysis): why B is fastest and why C slows down as the book grows
- [Reproduction](benchmark.md#reproduction): the exact commands we ran

Screenshots of the runs:

- Benchmark output: [Implementation B](screenshots/image%20copy%203.png), [Implementation C](screenshots/image%20copy%202.png)
- `perf stat` output: [Implementation A](screenshots/perf%20A/2.png), [Implementation B](screenshots/perf%20B/3.png), [Implementation C](screenshots/perf%20C/3.png)
- All screenshots: [screenshots/](screenshots/)

## Project layout

| Path | Contents |
|---|---|
| [order.h](order.h) | `Side`, `Level`, `BBO`, the `IOrderBook` interface and tick conversion helpers |
| [orderbook.h](orderbook.h) | Includes all three implementations; `OrderBook` is an alias for `OrderBookA` |
| [impl_a/](impl_a/), [impl_b/](impl_b/), [impl_c/](impl_c/) | The three implementations, each with a small demo `main` |
| [main.cpp](main.cpp) | Demo program using the default `OrderBook` |
| [verify.cpp](verify.cpp) | Unit tests and a cross-check of A, B and C on random operations |
| [bench.cpp](bench.cpp) | Benchmark harness |
| [benchmark.md](benchmark.md) | Benchmark and `perf` results with discussion |
| [screenshots/](screenshots/) | Screenshots of the benchmark and `perf` runs |

## Build

You need CMake 3.16 or newer and a C++20 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

This builds `OrderBook`, `OrderBookA`, `OrderBookB`, `OrderBookC`, `verify` and `bench` in `build/`.

## Check correctness

`verify` runs the unit tests and then runs the three order books on the same generated sequence of operations, comparing their results (default 25,000 operations):

```bash
./build/verify --verify --ops 100000
```

The benchmark also has a verification mode:

```bash
./build/bench --verify --ops 100000 --seed 42
```

## Run the benchmark

Run each implementation with the same options:

```bash
./build/bench --impl A --ops 100000 --runs 5 --seed 42
./build/bench --impl B --ops 100000 --runs 5 --seed 42
./build/bench --impl C --ops 100000 --runs 5 --seed 42
```

The seed makes the workload repeatable. It is about 70% adds, 20% cancels and 10% BBO queries, plus a depth query every 100 operations. The program reports median timings, page faults, peak memory use, the final BBO and a checksum.

The assignment asks for one million operations. We used 100,000 for the recorded comparison because Implementation C gets much slower as the book grows. The results are in [benchmark.md](benchmark.md).

## Profile with `perf`

Collect hardware counters for Implementation A:

```bash
perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  -- ./build/bench --impl A --ops 100000 --runs 1 --seed 42
```

Repeat with `--impl B` and `--impl C` to compare. Some counters may not be available on every machine.

To see where the time goes, record a profile and open it with `perf report`:

```bash
perf record -g -- ./build/bench --impl A --ops 100000 --runs 1 --seed 42
perf report
```

Repeat for B and C. The results and a short discussion are in [benchmark.md](benchmark.md).
