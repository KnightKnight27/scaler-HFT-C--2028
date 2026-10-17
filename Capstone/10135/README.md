# Low-Latency Order Book

Capstone submission: a single-instrument resting order book built three
different ways, benchmarked and profiled against the same workload.

- **Name:** Prateek Singh
- **Email:** prateek.24bcs10135@sst.scaler.com
- **Roll no:** 24BCS10135

There is no matching and no trade generation. Orders rest until they are
cancelled, and the quantity at a price level is the sum of the orders sitting
there.

## Layout

```
include/
  common.hpp        shared types (Side, Level, BBO) + price/tick helpers
  book_map.hpp      Implementation A: ordered std::map + hashed orders
  book_heap.hpp     Implementation B: binary heaps + lazy deletion
  book_array.hpp    Implementation C: flat price arrays + open-addressed orders
src/
  bench.cpp         one driver: benchmark, --verify, --test, workload generator
scripts/
  run_all.sh        build + verify + benchmark + perf, writes into results/
results/            captured correctness, benchmark and perf output
```

## Build

Requires a C++17 compiler. Everything is one translation unit, so either works:

```bash
make
# or, by hand:
g++ -O2 -std=c++17 -Iinclude src/bench.cpp -o bench
```

## Run

```bash
./bench --verify         # replay one workload through A, B, C and check they agree
./bench --test           # small hand-written edge-case checks
./bench --impl A         # benchmark one build (1,000,000 ops, 5 runs, median)
./bench --impl B
./bench --impl C
```

`--ops N` and `--runs N` override the defaults for quick experiments. The numbers
in the report use the defaults: one million operations, five runs, median taken.

To reproduce everything including perf on a Linux box:

```bash
bash scripts/run_all.sh
```

## Interface note

The public API keeps the required `double` price argument:

```cpp
void                add_order(uint64_t id, Side side, double price, long qty);
bool                cancel_order(uint64_t id);
BBO                 get_bbo() const;
std::vector<Level>  get_bids(int depth = 10) const;
std::vector<Level>  get_asks(int depth = 10) const;
```

Internally every price is immediately converted to an integer **tick** (the tick
size is 0.01, so `100.01 -> 10001`). Bids are ticks `5000..9999` and asks are
`10001..15000`, which is the 10,000 legal levels around the 100.00 mid.

Behaviour that is worth calling out:

- `cancel_order` on an unknown or already-cancelled id returns `false` and never
  throws or crashes.
- `get_bbo()` on an empty book returns `has_bid == false` / `has_ask == false`.
- `get_bids(10)` on a book with only three levels returns those three.
- `add_order` throws `std::invalid_argument` for a price that is not a clean
  tick, a price outside the side's band, a non-positive quantity, or a duplicate
  order id. (The benchmark never feeds invalid input; this just keeps the books
  honest. The edge-case behaviour is covered by `--test`.)

## The three designs

The three builds differ in how they store orders, store price levels, find the
best price, and locate an order for cancellation.

### A: ordered map (`book_map.hpp`)

- Price levels: two `std::map` keyed by tick, bids high-to-low and asks
  low-to-high, each mapping tick to total quantity.
- Orders: `std::unordered_map<id, OrderRec>`.
- Best price: `begin()` of the map.
- Cancel: hash lookup by id, then one tree update (erase the level if it empties).

### B: binary heaps with lazy deletion (`book_heap.hpp`)

- Price levels: a max-heap of bid ticks and a min-heap of ask ticks give the best
  price; the quantity per level lives in a separate `std::unordered_map`.
- Orders: `std::map<id, OrderRec>` (a tree, so this build also differs from A in
  where it keeps order records).
- Best price: heap top, after popping any stale ticks whose level is now empty.
- Cancel: tree lookup by id, update the quantity hash, and leave the stale tick
  in the heap to be cleaned up later. A tick is only pushed when a level goes
  from empty to non-empty, which keeps the heaps near the number of live levels.

### C: flat price arrays, my own design (`book_array.hpp`)

- Price levels: one flat array per side, indexed directly by tick, holding the
  total quantity. No tree, no heap, no pointer chasing.
- Best price: cached array indices (`best_bid_`, `best_ask_`), repaired by a
  short walk only when the current best level empties.
- Orders: my own open-addressed hash table (linear probing with tombstones and a
  splitmix64 hash), so order storage is also a flat array.
- Cancel: hash lookup in the order table, then a direct array index.

### Complexity

`N` = live orders, `P` = active price levels, `D` = requested depth, `R` = the
5,000-tick span on one side.

| Operation      | A                    | B                                  | C                        |
|----------------|----------------------|------------------------------------|--------------------------|
| `add_order`    | O(log P) + avg O(1)  | avg O(1) + O(log N) (order tree)   | avg O(1)                 |
| `cancel_order` | avg O(1) + O(log P)  | O(log N) + avg O(1), lazy on levels| avg O(1), worst O(R) repair |
| `get_bbo`      | O(1)                 | amortised O(1) (pop stale tops)    | O(1)                     |
| `get_bids(D)`  | O(D)                 | O(D) + popped stale/duplicate tops | O(R), stops after D      |
| `get_asks(D)`  | O(D)                 | O(D) + popped stale/duplicate tops | O(R), stops after D      |

## Correctness

`./bench --verify` builds all three, drives the identical one-million-operation
workload through each in lock step, and after every operation compares the BBO,
the top ten bids and the top ten asks. See `results/correctness.txt`.

The results table and the performance analysis are in [REPORT.md](REPORT.md).
