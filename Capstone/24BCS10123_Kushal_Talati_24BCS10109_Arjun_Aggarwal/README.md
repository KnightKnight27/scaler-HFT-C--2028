# Capstone - Low Latency Order Book

## Team Members

- Kushal Talati
    - email: kushal.24bcs10123@sst.scaler.com
    - roll no: 24BCS10123
- Arjun Aggarwal
    - email: arjun.24bcs10109@sst.scaler.com
    - roll no: 24BCS10109

## How to build

```bash
g++ -std=c++17 -O2 -o bench bench.cpp workload.cpp
```

On mac we used apple clang with the same flags (homebrew g++'s steady_clock only ticks every 1 us on mac).

## How to run the correctness test

```bash
./bench --verify
```

There is also a hand written edge case suite that runs on all three:

```bash
g++ -std=c++17 -O2 -o edge_tests tests/edge_cases.cpp && ./edge_tests
```

## How to run each benchmark

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

Each prints 5 timed runs and the median.

## perf

perf doesn't run on mac, so we ran it in an ubuntu docker container (only task-clock and page-faults work there, the other counters come from cachegrind in the same container, more in REPORT.md):

```bash
bash scripts/run_perf.sh     # linux: perf stat + perf record for A/B/C
bash scripts/run_macos.sh    # mac: build, verify, timed runs
```

All outputs are in `perf/` and `results/`.

## Interface changes

- Inside, prices are stored as integer ticks (99.99 -> 9999). The api still takes and returns `double`.
- `add_order` returns `bool`, false for a price outside the side's band, qty <= 0 or a duplicate id. `cancel_order` of an unknown id returns false.
- C uses the order id as an index. Ids above 2^20 go to a slower `unordered_map` fallback.
- Extra bench flags: `--runs R`, `--ops N`, `--seed S`, `--json`, `--notime`.

## Data structures

### Implementation A (std::map)

- price levels: `std::map<tick, qty>` per side (bids with `std::greater`)
- best bid / ask: `map::begin()`
- cancel: `unordered_map` from order id to order

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(log L) |
| `cancel_order` | O(log L) |
| `get_bbo` | O(1) |
| `get_bids` / `get_asks` | O(depth) |

L = number of price levels on that side (max 5000).

### Implementation B (std::priority_queue)

- price levels: `unordered_map` from tick to total qty
- best bid / ask: max heap / min heap of prices, empty levels are popped lazily when they reach the top
- cancel: `unordered_map` from order id to order

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(1), + O(log L) if the price is a new level |
| `cancel_order` | O(1) |
| `get_bbo` | O(1), + amortized O(log L) for popping empty levels |
| `get_bids` / `get_asks` | O(L + depth log L), copies the heap |

### Implementation C (array, our own design)

- price levels: one array per side with a slot for every tick (5000 slots), plus a bitmap of non empty slots
- best bid / ask: cached index, if the best level empties `__builtin_ctzll` on the bitmap finds the next one
- cancel: `std::vector` of orders where the index is the order id

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(1) |
| `cancel_order` | O(1), worst case O(L / 64) if the best level empties |
| `get_bbo` | O(1) |
| `get_bids` / `get_asks` | O(depth + bitmap words skipped) |
