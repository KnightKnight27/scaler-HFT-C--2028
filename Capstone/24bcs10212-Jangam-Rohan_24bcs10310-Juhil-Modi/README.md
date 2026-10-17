## Student details
- name: Jangam Rohan
- email: jangam.24bcs10212@sst.scaler.com
- roll number: 24BCS10212

teammate
- name: Juhil Modi
- email: juhil.24bcs10310@sst.scaler.com
- roll number: 24BCS10310


## Low latency order book (capstone)

A resting order book for one instrument (no matching, orders just sit until cancelled), built three different ways.
One benchmark program can run any of the three, and a `--verify` mode checks that all three give the same answers.

The results table, perf output and the analysis are in [REPORT.md](REPORT.md).

### Results at a glance
Reading raw perf output gets tiring, so we wrote a small python script (`plot_results.py`, uses matplotlib) that reads
everything in `results/` and turns it into charts. In short: C (the price array) is the fastest at everything, B (sorted
vector) has the slowest cancel because finding an order in its `std::map` means chasing pointers all over memory, and A sits in between.

How long each operation takes on average, and for the slowest 1% (p99):

![latency per op](results/graphs/latency_per_op.png)
![p99 latency](results/graphs/p99_latency.png)

Total time for the whole 1M op workload:

![total time](results/graphs/total_time.png)

Why it happens, from cachegrind (simulated, since WSL2 hides the real hardware counters) and perf:

![L1 misses](results/graphs/l1_misses.png)
![branch mispredicts](results/graphs/branch_mispredicts.png)
![instructions](results/graphs/instructions.png)
![page faults](results/graphs/page_faults.png)

To redraw them after a new run: `python plot_results.py`

### Files
- `common.hpp` - `Side`, `Level`, `BBO` and the price <-> tick helpers
- `book_a.hpp` - implementation A, `MapOrderBook`
- `book_b.hpp` - implementation B, `VectorOrderBook`
- `book_c.hpp` - implementation C, `ArrayOrderBook`
- `bench.cpp` - the benchmark and the correctness test
- `results/` - raw output from my machine (verify, bench, perf stat, perf report, cachegrind)
- `plot_results.py` - reads `results/` and draws the bar charts into `results/graphs/` (`python plot_results.py`, needs matplotlib)


### Build

with g++ directly
```bash
g++ -std=c++17 -O2 -Wall -Wextra bench.cpp -o bench
```

or with CMake
```bash
cmake -S . -B build
cmake --build build
```

For the perf runs I added `-g -fno-omit-frame-pointer` so perf can show function names and call stacks.
It is still `-O2`.
```bash
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer bench.cpp -o bench
```


### Run

correctness test
```bash
./bench --verify
```

benchmark (5 runs by default, median is printed)
```bash
./bench --impl A
./bench --impl B
./bench --impl C
./bench --impl C --runs 1
```

perf
```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A
perf record -g ./bench --impl A
perf report
```


### Interface changes
The public API is the same as in the assignment, prices go in and come out as `double`. A few small things:
- `BBO` fields have default values (`false` / `0`), so an empty side is just `has_bid == false` with zeros.
- Inside the books every price is stored as an integer number of ticks, `99.98 -> 9998` (`to_tick()`), and turned back into a double only when returning results (`to_price()`).
- Book C uses the order id as a vector index, so it assumes ids are small and given out in order (0, 1, 2 ...). The benchmark does that. Cancelling an unknown id still just returns `false`.
- There is no common base class. The three books just have the same method names and the benchmark is a template, so there is no virtual call inside the timed code.


### The workload
- 1,000,000 ops built up front with `std::mt19937 rng(42)`, so every implementation replays the exact same list
- 70% add, 20% cancel, 10% get_bbo
- side 50/50, qty 1-100, price is a random valid tick inside that side's band
- a cancel always picks a random order that is still live (if nothing is live yet it becomes an add)
- every 100 ops: `get_bids(10)` and `get_asks(10)`
- every op is timed on its own with `std::chrono::steady_clock`, the whole thing runs 5 times and the median is reported
- besides the average, the bench prints the p99 of each op type (99% of ops are faster than this). It's counted in a small 10 ns bucket histogram, so it doesn't need to store or sort a million numbers.
- a checksum of the quantities seen is printed at the end so the compiler can't skip the work. It's the same for all three books (491457988).


### The three designs

#### A - std::map levels + unordered_map orders (`MapOrderBook`)
- **orders**: `std::unordered_map<uint64_t, Order>`, id -> {side, tick, qty}
- **price levels**: one `std::map<int, long>` per side, tick -> total qty. It's a red-black tree, every level is its own heap node.
- **best bid / ask**: the map is always sorted, so best bid is `bids_.rbegin()` (highest) and best ask is `asks_.begin()` (lowest)
- **cancel**: hash lookup by id, then `map.find(tick)` to take away the qty, and erase the level if it hits 0

#### B - sorted vector levels + std::map orders (`VectorOrderBook`)
- **orders**: `std::map<uint64_t, Order>`, so finding an id is a tree search instead of a hash
- **price levels**: one `std::vector<TickLevel>` per side, kept sorted by tick from low to high, all in one block of memory
- **best bid / ask**: `bids_.back()` and `asks_.front()`
- **cancel**: tree lookup by id, then my own binary search for the level. If the level becomes empty it is erased from the vector, which shifts everything after it.

#### C - price indexed array + vector of orders (`ArrayOrderBook`)
- **orders**: `std::vector<Order>` where the index is the order id, with a `live` flag
- **price levels**: one `std::vector<long>` with 15001 slots, slot number = tick. There are only 10,000 valid prices, so no searching at all, just `qty_at_[tick]`.
- **best bid / ask**: kept in two ints, updated on every add. If a cancel empties the best level, it walks outward tick by tick to the next price that still has qty.
- **cancel**: `orders_[id]`, a direct index


### Complexity
L = non-empty levels on one side (max 5000), N = live orders, d = depth asked for, P = ticks on one side (5000)

| Operation | A | B | C |
|---|---|---|---|
| add_order | O(1) avg hash + O(log L) | O(log N) + O(log L), plus O(L) shift when it's a new price | O(1) (amortized, the orders vector sometimes grows) |
| cancel_order | O(1) avg hash + O(log L) | O(log N) + O(log L), plus O(L) shift when a level empties | O(1), or O(P) when the best level empties |
| get_bbo | O(1) | O(1) | O(1) |
| get_bids | O(d) | O(d) | O(d + empty ticks skipped), worst O(P) |
| get_asks | O(d) | O(d) | O(d + empty ticks skipped), worst O(P) |


### Correctness
`./bench --verify` does two things.

1. Hand-written edge cases on each book: empty book, cancelling an id that never existed, cancelling the same order twice, asking for 10 levels when only 2 exist, two orders at the same price adding up, and the best level emptying so the next one takes over. There's also a band-edge test with orders at exactly 50.00, 99.99, 100.01 and 150.00, a level that empties and then gets orders again, and the book going completely empty.
2. It replays random workloads through A and B side by side, and A and C side by side. After **every** op it checks the BBO, the top 10 bids, the top 10 asks and the cancel result. At the end it compares every level, not just the top 10. A is the reference.

There are two workloads. The normal one is the benchmark workload. With 700k adds over 10,000 prices every tick fills up early, so the best level almost never empties there.
The tight one keeps prices within 10 ticks of the middle and cancels more than it adds. That way levels, and whole sides, empty all the time, and that tests the "find the next best price" code properly.

I checked that the test can actually fail. I broke book C on purpose so it skipped one tick when looking for the next best bid. The normal workload still said PASS, but the tight workload caught it with 30715 mismatches. That's why the tight workload is there.

My output (`results/verify.txt`):
```
Running correctness test...

edge cases + band edges: A PASS, B PASS, C PASS

mismatches vs A, checked after every op:
  normal workload (1000000 ops, 70% add / 20% cancel / 10% bbo): B 0, C 0
  tight workload  (200000 ops, 45% add / 50% cancel, 10 ticks wide): B 0, C 0

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```
