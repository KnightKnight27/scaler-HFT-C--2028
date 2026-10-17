# Capstone - Low Latency Order Book

## Team Members

- Krritin Keshan
    - email: krritin.24bcs10122@sst.scaler.com
    - roll no: 24BCS10122
- Abdullah Danish
    - email: abdullah.24bcs10054@sst.scaler.com
    - roll no: 24BCS10054

## How to build

```bash
g++ -std=c++17 -O2 -o bench bench.cpp
```

On mac we used `clang++` with the same flags (homebrew g++'s steady_clock only ticks every 1 us on mac).

## How to run the correctness test

```bash
./bench --verify
```

## How to run each benchmark

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

## perf

perf doesn't run on mac, so we ran it in docker (only task-clock and page-faults work there, more in REPORT.md):

```bash
docker build -t ob-perf -f perf.Dockerfile .
docker run --rm --privileged -v "$PWD":/work -w /work ob-perf ./run_perf.sh
```

The mac does have hardware counters, so `./run_mac.sh` also reads the real branch, L1 and dTLB misses with Instruments (`xctrace`). For that you need a CPU Counters template saved in Instruments as "OB Counters", the 6 events are listed in the script.

`./run_mac.sh` runs verify, the benchmarks, `/usr/bin/time -l` and the Instruments counters on the mac. All outputs are in `results/`.

## Interface changes

No changes. Inside, prices are stored as integer ticks (99.99 -> 9999). C only takes order ids below 2^24 because it uses the id as an index.

## Data structures

### Implementation A (std::map)

- price levels: `std::map<int, PriceLevel>` per side, each level has a `std::list` of its orders
- best bid / ask: last / first element of the map
- cancel: `unordered_map` from order id to (side, tick, list iterator)

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(log L) |
| `cancel_order` | O(log L) |
| `get_bbo` | O(1) * |
| `get_bids` | O(depth) |
| `get_asks` | O(depth) |

L = number of price levels on that side (max 5000).
\* for bids it's O(log L) on mac, libc++ `rbegin()` walks down the tree.

### Implementation B (std::priority_queue)

- price levels: `unordered_map` from tick to total qty
- best bid / ask: max heap / min heap of prices, empty levels are popped lazily when they reach the top
- cancel: `std::map` from order id to order

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(log N), + O(log L) if the price is pushed |
| `cancel_order` | O(log N), + amortized O(log L) for popping empty levels |
| `get_bbo` | O(1) |
| `get_bids` | O(L + depth log L), copies the heap |
| `get_asks` | O(L + depth log L), copies the heap |

N = number of live orders.

### Implementation C (array, our own design)

- price levels: one `long` array with a slot for every tick (10,001 slots)
- best bid / ask: two ints, if the best level becomes empty we walk to the next slot with qty
- cancel: `std::vector` of orders where the index is the order id

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(1) amortized |
| `cancel_order` | O(1), worst case O(L) if the best level empties |
| `get_bbo` | O(1) |
| `get_bids` | O(depth + empty slots skipped) |
| `get_asks` | O(depth + empty slots skipped) |
