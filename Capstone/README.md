# Order Book: Performance Analysis

Email: naverdo.24BCS10076@sst.scaler.com
Roll No: 24BCS10076
Name: Naverdo Sandeep

Disclaimer: This readme is partially written by AI mainly on the setup and results compilation (not analysis) section. The Bench code is completely AI but the implementation code is handwritten.

## Designs

### Common components

For all implementations, I have changed the interface to

- Use int for all prices since the range is 10000 numbers and this avoids floating point precision errors
- An Order struct consisting of qty, price and Side. Order Id is always stored via the underlying datastructures index. The order struct is ordered optimally to reduce padding and alignment waste.

### Implementation A - Map

For the first implementation I used an ordered map to store the bids and sells price and qty so retrieving the best price was O(logn) and an unordered map of id and `Order` to store the orders so deleting was O(1) + 2*O(logn). Best ask/bid was obtained by taking the map.begin() of each ordered map. THe bids were stored in reverse sort to ensure its begin had the best (highest) bid.

During cancellation I iterate through the orders map using the required id and get the orders details before deleting it from the map. THen I reduce the qty from the relevant ask/bid map and if its qty goes to 0 then I delete them from that map as well.

| Operation | Time complexity | Explanation |
| --- | --- | --- |
| `add_order` | O(logn) | Ordered map (tree) is logn |
| `cancel_order` | O(logn) |  |
| `get_bbo` | O(logn) |  |
| `get_bids` | O(d*logn) | Iterating d times on the map |
| `get_asks` | O(d*logn) |  |

The main TC overhead was insertion/updates on a map which is O(logn).

In practice the bottleneck is memory, not the logn: every map node is a separate heap allocation, so each step down the tree jumps to a new cache line. Each step is also a left/right branch on a random price, so the CPU mispredicts.

### Implementation B - Priority Queue

For the second implementation I used two priority queues that only store prices, a max heap for bids and a min heap for asks, so the best bid/ask is just the top of each heap. The total qty of each price is stored in an unordered map of price and qty (`qts`). Since bid and ask prices never overlap, one map is enough for both sides. Orders are stored in an unordered map of id and `Order` like in A.

A heap can't delete from the middle, so I used lazy deletion. During cancellation I find the order by its id, reduce its qty from `qts` and delete the order. If the qty of that price goes to 0, I leave it in `qts` and in the heap as a dead price. It is only removed when it reaches the top of the heap in get_bbo/get_bids/get_asks, where I pop it and erase it from `qts` together. This way every price is in the heap only once, so adding to a price again doesn't push a duplicate. For get_bids/get_asks I pop the top d live prices into the result and then push them back, since a heap only gives the top element. Because of this, get_bbo, get_bids and get_asks change the heap and are not const in B.

| Operation | Time complexity | Explanation |
| --- | --- | --- |
| `add_order` | O(1) avg, O(logn) for a new price | Hash map update, heap push only when the price is new |
| `cancel_order` | O(1) avg | Unordered map lookup |
| `get_bbo` | O(1) + O(k*logn) | Top of each heap, after popping k dead prices (each dead price is popped only once) |
| `get_bids` | O(d*logn) | d pops and d pushes back |
| `get_asks` | O(d*logn) |  |

The main TC overhead was get_bids/get_asks which are O(d*logn), while add and cancel are O(1) on average.

In practice most of the runtime is the hash map lookups in add and cancel: each one does a `%` to find the bucket and then follows a pointer to a separately allocated node, which is often a cache miss. Branch mispredicts are almost zero since the lookups and the rare heap pushes are predictable.

### Implementation C - Price Indexed Array

For the third implementation I used one array `levels` indexed by price (`price - BASE`) that stores the total qty at each price for both sides. Bids are 50.00 to 99.99 and asks are 100.01 to 150.00, so bids fill the left half and asks the right half of the same array, and the best bid and best ask sit next to each other near the middle for best cache locality. Two ints `bestBid` and `bestAsk` work as the two pointers to the best prices. Orders are stored in a vector indexed by the order id, since ids are 1, 2, 3... and never reused, so no hashing is needed (the vector grows if a bigger id comes).

While adding I add the qty to `levels[price - BASE]` and update bestBid/bestAsk with a ternary instead of an if/else, so it compiles to `cmov` instead of a branch. During cancellation I get the order directly with `orders[id]`, reduce its qty from `levels` and set the order's qty to 0 to mark it as cancelled. If the best level becomes 0, I move bestBid down or bestAsk up until I reach a non-empty level. get_bbo just reads `levels` at bestBid and bestAsk, and get_bids/get_asks walk outward from the best price skipping empty levels. All the memory (`levels` and 1M orders) is allocated in the constructor, so nothing is allocated while the benchmark runs.

| Operation | Time complexity | Explanation |
| --- | --- | --- |
| `add_order` | O(1) | Array operations |
| `cancel_order` | O(1), O(Range) worst case | Scans to the next non-empty level when the best level empties (Range = 5000 prices per side) |
| `get_bbo` | O(1) |  |
| `get_bids` | O(d), O(Range) worst case | Skips empty levels |
| `get_asks` | O(d), O(Range) worst case |  |

The main TC overhead was cancel when the best level becomes empty, which can scan up to R levels, but this is rare.

In practice most of the runtime is cancel, where `orders[id]` with a random id reads from a 16 MB array that is usually not in L1/L2. Everything else is a few reads and writes on contiguous memory, so it has the fewest L1 misses and very few branch mispredicts (add has no branch at all).

## Setup

- **Machine:** Google Cloud `c4-standard-2` VM (2 vCPU), Intel Xeon Platinum 8581C @ 2.30 GHz (Emerald Rapids),
  L1d 48 KB, L2 2 MB, L3 260 MB. Ubuntu 24.04, kernel 7.0.0-1014-gcp, g++ 13.3, perf 7.0.14. The VM was created
  with `--performance-monitoring-unit=standard` so that perf can read the hardware counters.
- **Build:** `g++ -std=c++20 -O2 -g -o bench bench.cpp` (`-g` only adds debug info for `perf report`).
- **Timing:** `./bench --impl X`: 5 runs, median reported.
- **Counters:** `perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X --perf`
- **Profiles:** `perf record --call-graph dwarf ./bench --impl X --perf`, then `perf report --no-children`.
- `--perf` runs the workload once with no per-call timers. The plain `--impl X` mode calls `steady_clock::now()`
  around every operation, millions of calls per run, which would swamp the counters (on this VM, C uses 519 ms of
  CPU with timers against 53 ms without).
- **`cache-misses` is not supported** on this VM. Google Cloud only exposes last-level-cache events with the
  `enhanced` PMU, which needs a 144+ vCPU machine. Every other counter is available.
- Raw outputs are in [`results/`](results/).

## Correctness

`./bench --verify` runs the same seed-42 workload through A, B and C. After every single operation it compares the BBO and the top 10 bid and ask levels (prices and quantities) of B and C with A, and it also checks A on its own (bids strictly descending, asks strictly ascending, no level with qty 0, BBO equal to the first level). Before that it runs the cases from the assignment: an empty book has no bid or ask, cancelling an unknown or already cancelled id returns false, get_bids(10) with only 3 levels returns 3, and the best bid moves when its level becomes empty.

Output on the VM ([`results/verify.txt`](results/verify.txt)):

```text
Workload: 1000000 ops, seed 42: 700471 add, 199303 cancel, 100226 BBO, get_bids/get_asks(10) every 100 ops

Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

All three benchmark runs also print the same final BBO (99.99 x 1981 / 100.01 x 2573) and the same checksum (4500846651). I also ran `--verify` built with `-fsanitize=address,undefined`, and it found no memory errors or leaks.

## Results

| Metric | A | B | C |
| --- | ---: | ---: | ---: |
| Total time (ms) | 145.96 | 52.54 | **11.18** |
| ns / add | 145.6 | 50.3 | **7.7** |
| ns / cancel | 235.3 | 140.1 | **29.9** |
| ns / BBO | 5.6 | 18.4 | **1.9** |
| ns / get_bids(10) + get_asks(10) | 141.3 | 601.8 | **47.2** |
| IPC | 1.17 | 2.27 | **2.43** |
| Branch misses (%) | 6.27 | **1.39** | 3.84 |
| L1-dcache-load-misses | 11.72 M | 5.32 M | **1.20 M** |
| dTLB-load-misses | 369.6 K | 331.8 K | **32.9 K** |
| Cache misses | not supported | not supported | not supported |
| Page faults | 15,066 | 15,021 | **10,899** |

The timing rows are the median of 5 runs, with the 26 ns `steady_clock` overhead subtracted from per-call numbers.
The counter rows are whole-process `perf stat` (mean of 5 runs, spread at most 1.7%).

### Separating the book from the benchmark

Every run also builds the 1M-operation workload and runs the dispatch loop, which picks add, cancel or BBO from a
random op type. Both are the same for all three books, so I measured them on their own:

- `./bench --baseline` only builds the workload: 39 ms, 321 M instructions, 1.26 M branch misses and 6,971 page
  faults ([`perf_stat_baseline.txt`](results/perf_stat_baseline.txt)).
- The same loop with a book that does almost nothing ([`perf_stat_loop.txt`](results/perf_stat_loop.txt)) adds
  another 14 M instructions and **0.50 M branch misses**. The op type is random (70/20/10), so the
  `if add / else if cancel / else` in the loop mispredicts about once every two operations.

  ```cpp
  struct BookNull {   // does almost nothing, so a run measures only the benchmark loop
      long x = 0;
      void add_order(uint64_t id, Side, int price, long qty) { x += id + price + qty; }
      bool cancel_order(uint64_t id) { x ^= id; return true; }
      BBO get_bbo() const { BBO b{}; b.has_bid = true; b.bid_price = (int)x; return b; }
      std::vector<Level> get_bids(int) const { return {}; }
      std::vector<Level> get_asks(int) const { return {}; }
  };
  ```

The table below is each implementation's run minus the `BookNull` run, divided by the 1M operations:

| Per operation, book only | A | B | C |
| --- | ---: | ---: | ---: |
| cycles | 532 | 210 | **31** |
| instructions | 454 | 459 | **79** |
| IPC | 0.85 | 2.19 | **2.59** |
| branches | 96.2 | 87.1 | **10.1** |
| branch misses | 6.97 | **~0.05** | 0.28 |
| L1-dcache misses | 11.4 | 4.95 | **0.83** |
| dTLB misses | 0.36 | 0.32 | **0.02** |
| page faults (whole run) | 8,093 (31.6 MiB) | 8,048 (31.4 MiB) | **3,926 (15.3 MiB)** |

## Which implementation is fastest?

C is the fastest for add, cancel and get_bbo (and for get_bids/get_asks too).

- **C** only does a few array reads and writes per operation: no hashing, no tree and no memory allocation. Its slowest operation is cancel, because `orders[id]` with a random id is usually not in the cache.
- **B** is second. Add and cancel are just hash map lookups, and the heap only changes when a new price appears or a price dies. Its get_bbo is slower than A's because it reads the heap top and then looks up the qty in the hash map, and its get_bids/get_asks are the slowest of all three because they pop 10 prices and push them back.
- **A** is the slowest for add and cancel because each of them walks the tree (about 12 nodes deep) on top of the same hash map work as B. Its get_bbo is fast because `map.begin()` is O(1).

## Branch prediction

A has by far the most branch misses: about 7 per operation, while B and C are both below 0.3.

- **A:** every step down the tree is a left/right branch on a random price, so the CPU can't predict it and has to flush the pipeline. `perf record` shows most of A's misses inside the map's tree walk.
- **B:** hash map lookups and the rare heap pushes are predictable, so it has almost no misses.
- **C:** add has no branch at all (the ternary compiles to `cmov`). The only branches that depend on the data are probably the loops in get_bids/get_asks.

The branch miss % in the results table makes C look worse than B. That's because C has very few branches of its own, so the benchmark's random add/cancel/BBO choice (about 0.5 misses per operation, the same for all three) makes up most of its misses.

## Cache behavior

`cache-misses` isn't available on this VM, so I used the L1 and dTLB misses.

- **A has the most cache misses** (about 11 L1 misses per operation). It follows pointers: every tree node is a separate small allocation somewhere on the heap, so each step down the tree is a new cache line. It also allocates many small objects (one node per order and one per price).
- **B** (about 5 per operation) also follows pointers, from the hash bucket to the node, and allocates a node per order, but it has no tree walk. The heap itself is one flat array.
- **C** (under 1 per operation) accesses contiguous memory: `levels` is one array and `orders` is one array indexed by id. get_bids/get_asks walk neighbouring slots, so every cache line it loads is fully used.
- A and B also have over 10x more dTLB misses than C, because their nodes are spread over about 32 MB, while C's cancel touches only one page of `orders`.

## Page faults

A and B have the most page faults and C the fewest. About 7,000 of each run's page faults come from building the workload itself, so the rest is each book's own memory:

- **A and B: about 8,000 pages (32 MB).** This is the hash map's bucket array (from `reserve`) and one node for each of the ~500K live orders. The nodes are allocated while the benchmark runs, so these page faults happen inside the timed loop.
- **C: about 3,900 pages (16 MB).** This is exactly the `orders` vector (1M orders x 16 B) plus `levels`. Both are allocated and zeroed in the constructor before the timer starts, so C gets no page faults while running.

So A's and B's memory grows with the number of live orders, while C uses a fixed amount decided up front.

## Production choice

I would choose C.

- **Latency:** fastest for every operation.
- **Predictability:** no allocation, no page faults and very few branch misses while running, so the time per operation barely changes. The only slow case is a cancel that empties the best level and has to scan for the next one, and that was rare.
- **Memory usage:** fixed (80 KB of levels + 16 B per order id). A and B grow with the number of live orders.
- **Cache behavior:** contiguous arrays, with the best bid and ask next to each other in the middle of one array.
- **Complexity and maintainability:** it is the shortest and simplest code. B's lazy deletion was the hardest to get right.
- **Downside:** C only works because the price range is small and known (10,000 ticks) and order ids are small increasing numbers. A real exchange feed would need a hash map from its order id to a local index, and a much wider price range would waste memory.

A is the easiest to write and is fine for get_bbo, but its add and cancel are 10-20x slower. B is faster than A, but its get_bbo/get_bids cost depends on how many dead prices are waiting in the heap.

## What would you try next?


1. **C: `int` instead of `long` for the qty in `levels`.** The array becomes 40 KB, which fits in the 48 KB L1 cache.
2. **C: a smaller `Order`** (just `int price` and `int qty`, since the side can be found from the price). This halves the `orders` array.
3. **A and B: find orders by id with an array like C** instead of `unordered_map`, which needs a `%` and a pointer jump for every lookup.
