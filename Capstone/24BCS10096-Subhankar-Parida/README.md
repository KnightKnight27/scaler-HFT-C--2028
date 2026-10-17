# Capstone — Low Latency Order Book

## Team Members

- Subhankar Parida
    - email: subhankar.24bcs10096@sst.scaler.com
    - roll no: 24BCS10096

## How to Build

```bash
g++ -std=c++17 -O2 -o bench bench.cpp
```

On macOS, `clang++` with the same flags works too.

## How to Run the Correctness Test

```bash
./bench --verify
```

## How to Run Each Benchmark

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

## perf (Linux)

perf doesn't run on macOS natively, so we run it inside Docker:

```bash
docker build -t ob-perf -f perf.Dockerfile .
docker run --rm --privileged -v "$PWD":/work -w /work ob-perf ./run_perf.sh
```

For a single run without per-op timers (for use with perf/cachegrind):

```bash
./bench --impl A --perf
./bench --impl B --perf
./bench --impl C --perf
```

## Interface Changes

No changes to the required API. Internally, prices are stored as integer ticks (e.g., 99.99 → 9999). Implementation C requires order IDs below 2^24 because it uses the ID as a direct index into a vector.

## Data Structures

### Implementation A — `std::map` + `std::list`

- **Price levels**: `std::map<int, PriceLevel>` per side; each level has a `std::list` of its orders and a running total
- **Best bid / ask**: `rbegin()` / `begin()` of the map (always sorted)
- **Cancel**: `unordered_map` from order ID to `{side, tick, list iterator}` for O(1) lookup

| Operation | Complexity |
| --- | --- |
| `add_order` | O(log L) |
| `cancel_order` | O(log L) |
| `get_bbo` | O(1) amortized |
| `get_bids` | O(depth) |
| `get_asks` | O(depth) |

L = number of active price levels on that side (max 5000).

### Implementation B — `std::priority_queue` with Lazy Deletion

- **Price levels**: `unordered_map` from tick to total qty
- **Best bid / ask**: max-heap / min-heap of prices; empty levels are popped lazily when they reach the top
- **Cancel**: `std::map` from order ID to `{side, tick, qty}`

| Operation | Complexity |
| --- | --- |
| `add_order` | O(log N) + O(log L) if price is pushed to heap |
| `cancel_order` | O(log N) + amortized O(log L) for popping empty levels |
| `get_bbo` | O(1) |
| `get_bids` | O(L + depth·log L), copies the heap |
| `get_asks` | O(L + depth·log L), copies the heap |

N = number of live orders.

### Implementation C — Fixed-Size Array (Our Own Design)

- **Price levels**: one `long` array with a slot for every possible tick (10,001 slots, ~80 KB)
- **Best bid / ask**: two `int` trackers; if the best level becomes empty, walk to the next slot with qty
- **Cancel**: `std::vector<Order>` where the index IS the order ID — direct O(1) access

| Operation | Complexity |
| --- | --- |
| `add_order` | O(1) amortized |
| `cancel_order` | O(1), worst case O(L) if the best level empties |
| `get_bbo` | O(1) |
| `get_bids` | O(depth + empty slots skipped) |
| `get_asks` | O(depth + empty slots skipped) |
