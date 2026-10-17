# Capstone: Low Latency Order Book

- GitHub: Divii2205
- SST email: divijaa.24bcs10206@sst.scaler.com
- Roll number: 24BCS10206

This is a resting order book for one instrument. There is no matching. Orders are added, they wait in the book, and they leave only when cancelled. I built the same book three times with three different data structures, checked that all three give the same answers, and then benchmarked them.

The full results table and the performance analysis are in [REPORT.md](REPORT.md).

## Files

| File | What it has |
| ----- | ----- |
| `common.h` | `Side`, `Level`, `BBO`, `Order`, the tick constants and the input check |
| `book_map.h` | Implementation A, `std::map` for everything |
| `book_heap.h` | Implementation B, `std::priority_queue` with lazy deletion and hash maps |
| `book_array.h` | Implementation C, my own design with flat arrays |
| `bench.cpp` | one program for the benchmark and the `--verify` test |
| `run_perf.sh` | runs `perf stat` and `perf record` for A, B and C (Linux only) |
| `mac_stats.sh` | runs `/usr/bin/time -l` for A, B and C (macOS) |
| `results/` | the output I got from my runs |

## How to build

```
g++ -O2 -std=c++20 -Wall -o bench bench.cpp
```

I used `-std=c++20` only because of the `[[likely]]` and `[[unlikely]]` hints we saw in class. Everything else is C++17. Only the standard library is used.

## How to run the correctness test

```
./bench --verify
```

This does two things:

1. Small hand made tests for every case in the "Required Behavior" part of the doc: empty book, cancel of an unknown id, cancel of the same id two times, asking for 10 levels when only 3 exist, a level going empty and the best price moving, and orders with a price outside the band.
2. The same 1,000,000 op random workload (seed 42) goes through A, B and C at the same time. After every op the BBO of B and C is compared with A. Every 100 ops the top 10 bid and ask levels are compared. Every 10,000 ops the full book (every price and every qty) is compared. The return value of every cancel is also compared. On top of that, the total qty in each book is compared with a total that is worked out only from the list of ops, so A is also checked by something other than itself.

My output is in `results/verify_output.txt`:

```
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## How to run each benchmark

```
./bench --impl A
./bench --impl B
./bench --impl C
```

Each one runs the same 1,000,000 ops 5 times and prints the median. It prints total time, average ns per add, cancel and BBO, the time for the depth queries, p99 numbers, the final BBO and a checksum. All three print the same checksum, which is another sign that they did the same work. `--runs N` changes the number of runs.

The workload follows the doc: about 70% add, 20% cancel and 10% BBO, 50/50 buy and sell, qty from 1 to 100, a random valid tick in the right band, cancels always pick a live order, and every 100 ops there is a `get_bids(10)` and a `get_asks(10)`. The ops are made before the timer starts, so random number work is not timed.

## How to run perf

On Linux:

```
./run_perf.sh
```

It runs this for A, B and C and saves the output in `results/`:

```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A --runs 1
perf record -g ./bench --impl A --runs 1
perf report
```

`--runs 1` is used here because `perf stat -r 5` already repeats the program 5 times.

My laptop is a Mac and `perf` does not exist on macOS, so on the Mac I used `./mac_stats.sh`, which gives instructions, cycles, page reclaims and max memory from `/usr/bin/time -l`.

## Interface changes

The API is the same as the doc except for one change: **prices are integer ticks, not double**.

```cpp
void add_order(uint64_t id, Side side, int32_t tick, long qty);
bool cancel_order(uint64_t id);
BBO get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

- The tick size is 0.01, so price 100.01 is tick 10001 and price 99.98 is tick 9998.
- `Level` has `int32_t tick` instead of `double price`.
- `BBO` has `bid_tick` and `ask_tick` instead of `bid_price` and `ask_price`.
- Prices are turned back into the normal form only when printing (`print_price` in `bench.cpp`), and that also uses integer math.

Why: a double can not store 0.01 steps exactly, so two prices that should be the same can end up as different map keys. Integer compare and add is also cheaper than float work. It also lets Implementation C use the price directly as an array index.

Other small rules that all three books follow the same way:

- An add with qty 0 or less, or with a price outside its band (for example a buy above 99.99), is ignored.
- An add with an id that is already live is ignored.
- If a side is empty, `has_bid` or `has_ask` is false and the tick and qty are 0.

## Data structure choices

There are two separate problems in an order book (hint 4 in the doc):

- **per order**: order id to (side, tick, qty), needed for cancel
- **per price**: tick to total qty, needed for BBO and depth

Each design solves both in a different way.

### Implementation A: `std::map` (a tree for everything)

1. **Data structures**: three `std::map`s. `std::map` is a red black tree.
2. **Orders**: `std::map<uint64_t, Order>` from id to order.
3. **Price levels**: `std::map<int32_t, long, std::greater<>>` for bids (high to low) and `std::map<int32_t, long>` for asks (low to high). The value is the total qty.
4. **Best bid and ask**: `begin()` of each level map. The map keeps a pointer to its leftmost node so this is cheap.
5. **Cancel lookup**: `find` in the order map, then `find` in the level map, subtract the qty and erase the level if it is 0.

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(log N + log L) |
| `cancel_order` | O(log N + log L) |
| `get_bbo` | O(1) |
| `get_bids` | O(d) |
| `get_asks` | O(d) |

N is the number of live orders, L is the number of live price levels on one side (at most 5000), d is the depth asked for.

### Implementation B: `std::priority_queue` with lazy deletion

1. **Data structures**: two heaps (`std::priority_queue`) and two hash maps (`std::unordered_map`).
2. **Orders**: `std::unordered_map<uint64_t, Order>` from id to order.
3. **Price levels**: `std::unordered_map<int32_t, long>` from tick to total qty. Each live tick is also pushed once into a heap: a max heap for bids and a min heap for asks.
4. **Best bid and ask**: `top()` of each heap. A heap can not remove something from the middle, so when a level becomes 0 it stays in the heap as a dead entry (lazy deletion, like we did in class). Dead entries are popped only when they reach the top. I do this cleaning inside `cancel_order`, so `get_bbo` stays O(1) and `const`.
5. **Cancel lookup**: `find` in the order hash map, then `find` in the level hash map.

A tick is in a heap only while it has an entry in the level hash map, so the same tick is never pushed two times. If a dead level gets a new order before it is popped, it just becomes alive again.

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(1) average, O(log L) when the price is new |
| `cancel_order` | O(1) average, plus O(log L) for each dead entry popped from the top |
| `get_bbo` | O(1) |
| `get_bids` | O(L + d log L) because the heap has to be copied and popped |
| `get_asks` | O(L + d log L) |

L here also counts dead entries still inside the heap.

### Implementation C: flat price array plus order pool (my own design)

1. **Data structures**: one fixed array for all price levels, one `std::vector` used as an order pool, and two cached best indexes.
2. **Orders**: `std::vector<Order>` where the order id is the index. It is sized for about one million orders in the constructor, so there is one big allocation at the start (the memory pool idea from the lecture 9 homework). A qty of 0 means the slot is free or cancelled. Each `Order` is 16 bytes because I ordered the fields from big to small to avoid padding (lecture 7).
3. **Price levels**: `long levels_[10001]`, one slot for every tick from 50.00 to 150.00. Index is `tick - 5000`. Bids are on the left of the mid and asks are on the right, in the same block of memory, like the idea sir showed from the paper.
4. **Best bid and ask**: two cached indexes, `best_.bid` and `best_.ask`, kept together in one 64 byte aligned struct. An add can only make the best price better, so it is one compare. A cancel only has to search when it empties the best level, then it walks away from the mid to the next slot that has qty.
5. **Cancel lookup**: `pool_[id]`, a direct index, no hashing and no tree.

| Operation | Complexity |
| ----- | ----- |
| `add_order` | O(1) (amortized if the pool has to grow) |
| `cancel_order` | O(1) usually, O(P) worst case when the best level empties and the next one is far |
| `get_bbo` | O(1) |
| `get_bids` | O(d + empty slots skipped), worst case O(P) |
| `get_asks` | O(d + empty slots skipped), worst case O(P) |

P is the number of price slots on one side, which is 5000.

The weak point of this design is the order pool. It assumes ids are small numbers that go up one by one, like an exchange gives. Very large or random ids would make the pool huge. In that case I would put a hash map from id to pool slot in front of it.

## Results in short

On my Mac (Apple Silicon, Apple clang, `-O2`), median of 5 runs:

| | A (map) | B (heap) | C (array) |
| ----- | ----- | ----- | ----- |
| Total time (ms) | 338.54 | 153.88 | 69.32 |
| ns / add | 209.3 | 87.4 | 31.6 |
| ns / cancel | 747.9 | 191.2 | 53.8 |
| ns / BBO | 28.7 | 27.8 | 28.5 |

More numbers and the analysis are in [REPORT.md](REPORT.md).
