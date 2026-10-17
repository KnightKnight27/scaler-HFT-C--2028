# Low-Latency Order Book in C++17

**Name:** Swarnika Somvanshi
**Roll Number:** 24bcs10482
**Email:** swarnika.24bcs10482@sst.scaler.com

I built a single-instrument resting order book in C++17 three different ways: a `std::map` version, a sorted-vector version, and a price-indexed array version. I benchmarked all three on the same 1,000,000-operation workload and used Linux `perf` to find out why the array version is about 2.5x faster than the other two.

---

## 1. The three designs

| Design | Idea |
|---|---|
| **A** | One `std::map` per side for price levels (price -> total quantity) and a `std::unordered_map` for orders (id -> order). This is my "standard library" baseline. |
| **B** | One sorted `std::vector` per side for price levels, with the best price at the back. Orders go in one append-only `std::vector` that I search by ID with binary search. |
| **C** | My own design. A fixed array of 5,000 quantities per side, indexed directly by price. Orders sit in a `std::vector` indexed directly by order ID. The best bid and best ask are cached as array indexes. |

## 2. Files

| File | What it is |
|---|---|
| `common.h` | Shared types (`Side`, `Level`, `BBO`), price constants, tick conversion and array index helpers |
| `implem_a.h` | Implementation A (`std::map` + `std::unordered_map`) |
| `implem_b.h` | Implementation B (sorted vectors + binary search) |
| `implem_c.h` | Implementation C (price-indexed arrays) |
| `workload.h` | Generates the same 1M operations every time (`std::mt19937`, seed 42) |
| `test.cpp` | Edge-case tests, then three workloads compared against a reference book after every operation |
| `mutation_lab.py` | Applies each deliberate bug to a temporary copy, runs `test.cpp`, and saves the output to `results/mutation_*.txt` |
| `bench.cpp` | One benchmark program for all three, chosen with `--impl A\|B\|C` |
| `run_all.sh` | Compiles, runs the tests and benchmarks, then collects `perf stat` and `perf record` data |
| `results/` | Saved output: test result, benchmark results, `perf stat` files and `perf record` reports |

## 3. Build and run

Build and run the correctness test:

```bash
g++ -O2 -std=c++17 test.cpp -o test
./test
```

Build and run each benchmark (5 runs each, median reported):

```bash
g++ -O2 -std=c++17 bench.cpp -o bench
./bench --impl A
./bench --impl B
./bench --impl C
```

Pinned to one core, with `perf`:

```bash
taskset -c 2 ./bench --impl A

taskset -c 2 perf stat -r 5 \
  -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
  ./bench --impl A

taskset -c 2 perf record -F 999 -g ./bench --impl A --runs 1
perf report --stdio --no-children
```

I repeated the `perf` commands for `--impl B` and `--impl C`. `./run_all.sh` does all of this in one go and saves the output in `results/`.

To redo the deliberate-bug experiments (they run on a temporary copy, so my source files are not touched):

```bash
python3 mutation_lab.py
```

`--runs N` is an optional flag on `bench` (default 5). I used `--runs 1` for `perf record` because recording five passes made it very slow on my laptop.

## 4. Interface changes

- **Integer ticks instead of `double` prices.** A price like 99.98 cannot be stored exactly in a `double`, and comparing doubles is a source of subtle bugs. Since the tick size is fixed at 0.01, I convert every price to an integer number of ticks (99.98 -> 9998, 100.01 -> 10001) using `llround(price * 100)`. I used `llround` because `99.98 * 100` can come out as 9997.999..., which a plain cast would truncate to 9997. Inside the book there are only integers. I turn ticks back into decimals only when printing.
- **New signature:** `add_order(uint64_t id, Side side, int tick, long qty)`. `Level` and `BBO` store ticks (`tick`, `bid_tick`, `ask_tick`) instead of `double` prices.
- **`const` methods:** `get_bbo`, `get_bids` and `get_asks` are all `const` in all three implementations. I did not need any lazy deletion, so nothing had to become non-const.
- **Order ID assumption (B and C):** both assume IDs only increase and start small (1, 2, 3, ...). That is what my workload generates. B needs this because its order vector has to stay sorted by ID for binary search. C needs it because it uses the ID as an array position. A does not rely on it.
- **No price validation inside the book:** `add_order` does not check that a tick is inside the allowed band, because the workload only generates valid ticks and I wanted to keep the hot path short. In C an out-of-range tick would write outside the array. `common.h` has an `is_valid_tick` helper that I use in my tests, but a real system would need to call it on input.

## 5. Data structure details

### Implementation A: `std::map` + `std::unordered_map`

- **Data structures:** `std::map<int,long>` per side (bids use `std::greater` so the highest price comes first) and `std::unordered_map<uint64_t, Order>` for orders.
- **Orders:** the hash map stores each order's side, tick and quantity under its ID.
- **Price levels:** each level is one entry in the side's map, holding the total quantity at that price. A level is erased when its quantity reaches 0.
- **Best bid/ask:** `begin()` of each map.
- **Cancel:** look up the ID in the hash map, find the level in the map, subtract the quantity, erase the level if it is empty, then erase the order. A second cancel finds nothing in the hash map and returns `false`.

| Operation | Complexity |
|---|---|
| `add_order` | O(log L) for the map, O(1) average for the hash map |
| `cancel_order` | O(log L) |
| `get_bbo` | O(1) |
| `get_bids` / `get_asks` | O(depth) |

(L = number of price levels currently in the book.)

### Implementation B: sorted vectors

- **Data structures:** one `std::vector<Level>` per side and one `std::vector<Order>` for orders.
- **Orders:** pushed to the back of one vector as they arrive. Since IDs only increase, the vector is already sorted by ID. Cancelling sets a `live` flag to false instead of erasing the order, so the vector never shifts and dead orders stay in memory.
- **Price levels:** each side is a sorted vector with the best price at the **back**. Bids are sorted low to high and asks high to low, so the best price is at the back for both. This keeps inserts and erases near the best price cheap. I wrote the search once and flip the comparison for asks with a `before()` function.
- **Best bid/ask:** `back()` of each vector.
- **Cancel:** binary search the order vector by ID, check it exists and is live, mark it dead, binary search the level vector, subtract the quantity, and erase the level if it is empty (this shifts elements).

| Operation | Complexity |
|---|---|
| `add_order` | O(log L) search + O(L) worst case for an insert that shifts elements; O(1) amortized for the order push |
| `cancel_order` | O(log N) for the ID search + O(log L) level search + O(L) worst case if the level is erased |
| `get_bbo` | O(1) |
| `get_bids` / `get_asks` | O(depth) |

(N = number of orders ever added, L = number of price levels.)

### Implementation C: price-indexed arrays (my own design)

- **Data structures:** two `std::array<long, 5000>` for quantities (one per side), two integers `best_bid_` and `best_ask_`, and a `std::vector<Order>` indexed by ID.
- **Orders:** `orders_[id]` is the order itself, so cancel never searches. A `live` flag (false by default) makes unknown IDs and double cancels return `false`.
- **Price levels:** the tick is the array index (`tick - 5000` for bids, `tick - 10001` for asks). The two arrays together cover all 10,000 possible price levels and take 80 KB.
- **Best bid/ask:** cached in `best_bid_` / `best_ask_`. A new order that is better than the current best simply replaces it. "No bids" is `best_bid_ = -1` and "no asks" is `best_ask_ = 5000`.
- **Cancel:** subtract the quantity at the order's index. Only if that level was the best one and is now empty, scan towards the next non-empty level.

| Operation | Complexity |
|---|---|
| `add_order` | O(1) amortized (the order vector sometimes has to grow) |
| `cancel_order` | O(1), or O(P) when the best level empties and I scan (P <= 5000) |
| `get_bbo` | O(1) |
| `get_bids` / `get_asks` | O(depth + empty slots skipped) |

## 6. Correctness and verification

**Edge cases.** `test.cpp` runs the same small tests on each implementation: an empty book (`has_bid` and `has_ask` are false), asking for more depth than there are levels (it returns what exists), cancelling an unknown ID, cancelling the same order twice, two orders at one price adding up, the best price moving after the best level is cancelled, the 10-level depth limit, and the lowest and highest ticks of each side (5000, 9999, 10001, 15000).

**Workloads.** I wrote a deliberately simple reference book using only plain `std::map`s. I compare each implementation against it after **every** operation: the cancel result, the BBO, and the top 10 bid and ask levels. I run three workloads through the reference and A, B and C together:

1. The 1,000,000-op benchmark workload (full price band).
2. A 200,000-op cancel-heavy workload on a 10-tick band near the mid price. The book empties often, which the full-band workload almost never does.
3. A 200,000-op cancel-heavy workload on a 10-tick band at the edges of the allowed range (bids at ticks 5000 to 5009, asks at 14991 to 15000). It also cancels some already-cancelled orders on purpose, to check that a double cancel returns `false`.

**Mutation testing.** To check that the tests really catch bugs, I broke each implementation on a temporary copy (`mutation_lab.py`, my real source is never touched), ran `./test`, and saved the real output in `results/`:

- **A:** I removed the line that erases a level when its quantity reaches 0, so empty levels stayed in the map. Result: `FAIL`, 115,927 mismatches, first at op 1 (the first cancel), plus failures in the small tests. B and C still passed. Output: `results/mutation_A.txt`.
- **B:** I made `before()` always compare `a < b`, so ask levels were sorted the wrong way and the best ask became the highest price. Result: `FAIL`, 2,799,892 mismatches, first at op 11. The count is bigger than the number of operations because one operation can fail several comparisons, and a broken book keeps failing on later operations. Output: `results/mutation_B.txt`.
- **C:** I changed the bid scan from `best_bid_ >= 0` to `best_bid_ > 0`, so after the last bid at tick 5000 was cancelled, `best_bid_` stayed at index 0 instead of becoming -1 and an empty book still reported a bid. Result: `FAIL`, 4 mismatches, first at op 7, plus a failure in the small tests. Output: `results/mutation_C.txt`.

The C mutation taught me something about my own tests. At first, only the small tests caught it, and the random 1M-op workload reported no mismatches, because with random prices over 5,000 levels neither side ever empties completely. The near-mid-price band did not catch it either, because its bids sit far from array index 0. Only the edge-band workload reaches the bug. So I added the two narrow workloads and the check after every operation, and I learned that a large random workload can miss boundary bugs, which is why targeted workloads and small tests matter.

Output of the unbroken code (`results/test_output.txt`):

```
Running small-case tests...
  A small tests:
    all passed
  B small tests:
    all passed
  C small tests:
    all passed

Running workload comparisons (BBO and top-10 depth checked after every op)...
  1,000,000 ops, full price band (the benchmark workload)
    mismatches: A=0  B=0  C=0
  200,000 ops, cancel-heavy, 10-tick band near the mid price
    mismatches: A=0  B=0  C=0
  200,000 ops, cancel-heavy, 10-tick band at the range edges (5000 / 15000)
    mismatches: A=0  B=0  C=0

Results:
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

The benchmark gives a second, independent check. All three implementations finish with the same final BBO and the same checksum.

## 7. Benchmark results

**Workload:** 1,000,000 operations from `std::mt19937 rng(42)`: about 70% add (700,471), 20% cancel (199,303), 10% BBO query (100,226). Sides are 50/50, quantities are 1-100, prices are random valid ticks in the side's band, and cancels only pick orders that are live. Every 100 operations the benchmark also calls `get_bids(10)` and `get_asks(10)`. I generate the workload once before timing starts. Time is measured with `std::chrono::steady_clock`: the total for the loop, and a separate timer around each add, cancel and BBO call. Each implementation runs 5 times on a fresh book, and I report the median.

```
A: MEDIAN total 333.2ms | add 289.8ns (p99 432) | cancel 454.3ns (p99 906) | bbo 27.8ns (p99 37)
Final BBO:
  Bid: 99.99 x 2722
  Ask: 100.01 x 2525
Checksum: 75482931624
B: MEDIAN total 311.4ms | add 215.1ns (p99 1239) | cancel 619.2ns (p99 1489) | bbo 28.9ns (p99 42)
Final BBO:
  Bid: 99.99 x 2722
  Ask: 100.01 x 2525
Checksum: 75482931624
C: MEDIAN total 128.7ms | add 79.0ns (p99 86) | cancel 132.5ns (p99 297) | bbo 33.1ns (p99 41)
Final BBO:
  Bid: 99.99 x 2722
  Ask: 100.01 x 2525
Checksum: 75482931624
```

The checksum mixes in every cancel result, every BBO result and the depth queries, and it is printed at the end, so the compiler cannot remove the work.

### Environment

- **Machine:** 13th Gen Intel Core i5-13500H, 16 logical CPUs, 15 GB RAM. L1d cache is 448 KiB in total, L2 is 9 MiB and L3 is 18 MiB. This is a hybrid chip with performance and efficiency cores.
- **Software:** Ubuntu 24.04.4 LTS, Linux 7.0.0-28-generic, g++ 13.3.0, compiled with `-O2 -std=c++17`. I ran this on the machine directly, not in a VM or WSL2.
- **CPU pinning:** I ran everything with `taskset -c 2`, so the OS cannot move the program between cores in the middle of a run. Core 2 is a performance core.
- **Which counters worked:** because the program ran on a performance core, only the `cpu_core/...` counters counted. The `cpu_atom/...` lines show `<not counted>` (and `<not supported>` for `L1-dcache-load-misses`) because the program never ran on an efficiency core. All the counters I asked for were available on the performance core.
- **Timer overhead:** every add, cancel and BBO call is wrapped in its own `steady_clock::now()` pair, so each latency includes the cost of reading the clock (tens of nanoseconds). This is the same for all three implementations, but it matters most for BBO, which is only a few nanoseconds of real work. In the `perf record` profile, `clock_gettime` took 15.7% of C's samples.
- **Run-to-run variation:** my laptop's speed changed between sessions. My first unpinned benchmark run showed A at 967.8 ms and C at 164.8 ms, and a later run during `run_all.sh` showed C at 251.1 ms. The pinned run above (333.2 / 311.4 / 128.7 ms) is the one I use in the tables. The `perf stat` counters were collected in the earlier, slower session (about 4.4-4.7 s task-clock per 5-pass process for A and B), so I only use them to compare the designs with each other (IPC, miss rates), not to explain the exact nanosecond numbers.
- **`perf stat` scope:** `perf stat` counts the whole process, including workload generation and all 5 passes, not just the timed loop.

## 8. Master results table

Timing rows come from the pinned benchmark above. The hardware rows come from `perf stat -r 5` on core 2.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 333.2 | 311.4 | 128.7 |
| ns / add | 289.8 | 215.1 | 79.0 |
| ns / cancel | 454.3 | 619.2 | 132.5 |
| ns / BBO | 27.8 | 28.9 | 33.1 |
| IPC | 0.75 | 0.77 | 1.46 |
| Branch misses (%) | 5.48% | 8.06% | 1.86% |
| L1-dcache-load-misses | 70.4 M | 137.4 M | 8.9 M |
| Cache misses | 51.9 M | 95.3 M | 11.7 M |
| Page faults | 21,663 | 77,078 | 59,409 |

Raw `perf stat` counters (`cpu_core`), for reference:

| Counter | A | B | C |
|---|---|---|---|
| task-clock (ms) | 4,698.96 | 4,437.22 | 1,549.50 |
| cycles | 5,307,229,086 | 4,565,807,895 | 1,581,508,947 |
| instructions | 4,002,958,817 | 3,508,923,122 | 2,309,673,893 |
| branches | 776,579,582 | 625,565,748 | 379,722,963 |
| branch-misses | 42,589,912 | 50,419,011 | 7,060,972 |
| cache-misses | 51,895,772 | 95,254,051 | 11,653,197 |
| L1-dcache-load-misses | 70,355,614 | 137,426,457 | 8,942,226 |
| dTLB-load-misses | 4,154,038 | 6,693,518 | 437,758 |
| page-faults | 21,663 | 77,078 | 59,409 |

IPC = instructions / cycles. Branch miss % = branch-misses / branches.

### What `perf record` showed

I recorded one pass of each implementation with `perf record -F 999 -g`. The compiler inlined most of the book's functions into the benchmark loop (`run_once`), so only some functions show up on their own.

- **A:** `run_once` 31.5%, `OrderBookA::cancel_order` 19.6%, `clock_gettime` 5.8%, `unordered_map::operator[]` 4.7%, and `_Hashtable::_M_rehash` 3.9%. Cancel and the hash table are the visible costs.
- **B:** `run_once` 34.0%, `SideBook::add` 19.8%, `clock_gettime` about 12% in total, and `memmove` 2.8%. The `memmove` is the element shifting inside vector inserts and erases.
- **C:** `run_once` 18.2%, `clock_gettime` 15.7%, and `vector<Order>::_M_default_append` 3.0% (the `resize` in `add_order`). The timer costs about as much as the book itself, and `generate_workload` plus the random number generator take about 26%, because `--runs 1` makes the setup a bigger share of a short run.

Kernel symbols could not be resolved because I ran `perf` without root, and a few call stacks look broken (long lists of raw addresses) because `-O2` does not keep frame pointers. Neither affects the function-level percentages above.

## 9. Performance analysis

### 11.1 Which implementation is fastest?

C is the fastest for add (79.0 ns) and cancel (132.5 ns) and has the lowest total time (128.7 ms), about 2.5x faster than A (333.2 ms) and B (311.4 ms). Add and cancel in C are plain array reads and writes: no searching, no hashing and no allocating a node. C also has the best tail latency (add p99 of 86 ns, against 432 for A and 1239 for B).

For add, B beats A (215 ns against 290 ns). I think this is because inserting into a contiguous vector near the end is cheaper than allocating a tree node and a hash node for every new order. For cancel, B is the slowest (619 ns): it does two binary searches and then possibly an erase that shifts elements. Adds make up 70% of the workload, which is why B's total time still ends up slightly below A's.

For BBO, A (27.8 ns) and B (28.9 ns) are a little faster than C (33.1 ns). The gap is about 5 ns, and each call also includes a timer read, so I consider all three roughly equal. My guess is that A and B read the first or last element of a structure that is already hot in cache, while C converts an index to a tick and reads from two different places. I did not measure this.

### 11.2 Branch prediction

B had the most branch misses (8.06%), then A (5.48%), then C (1.86%). A binary search makes one data-dependent branch per step, and because my prices are random, the CPU cannot predict which way those branches go. B does this several times per operation (searching the levels, and searching the order vector on cancel). A has the same problem walking down the `std::map` tree. Cancels in B also branch on whether a level exists and whether it becomes empty, which is random as well. C does almost no searching. Its loops usually run once or a few times, and its main unpredictable branch is the occasional scan after the best level empties. B has more branch misses than A even though it executes fewer branches in total (625 M against 777 M): each of B's branches is harder to predict.

### 11.3 Cache behavior

B had the most cache misses (95.3 M) and L1 data cache load misses (137.4 M), then A (51.9 M and 70.4 M), and C had far fewer than both (11.7 M and 8.9 M).

- **A** follows pointers. Every `std::map` node and every hash node is a separate small allocation, so one operation can touch several cache lines that are far apart in memory.
- **B** is contiguous, but it jumps around inside big vectors. A binary search over a large vector touches cache lines spread across it, and inserts and erases move many elements with `memmove`. The order vector also grows with every order, since dead orders are never removed.
- **C** reads arrays at addresses it can compute directly. The two quantity arrays are 40 KB each, and in practice the operations hit the same levels near the best price repeatedly. The order vector is read by ID, which is still a scattered access for random cancels, but it is only one access.

The same pattern shows up in the dTLB misses: 4.2 M for A, 6.7 M for B and 0.44 M for C.

### 11.4 Page faults

B had the most page faults (77,078), then C (59,409), then A (21,663). I expected C to have the fewest, so this surprised me. I did not investigate it directly, so the following is my best explanation and not a measured result.

B and C store orders in one big vector (about 700,000 entries, around 17-22 MB), and a vector that size is allocated straight from the OS. Each time it grows, a new, bigger block is allocated and the old contents are copied into fresh pages that have to be touched for the first time. My benchmark also creates a new book for each of the 5 passes, and large blocks are given back to the OS when they are freed, so the same pages fault again every pass. In C, `resize(id + 1)` shows up in the profile as `_M_default_append`, so the growing order vector is a visible cost. A's memory is mostly many small nodes, which the allocator can reuse across passes without going back to the OS. So the page-fault count here says more about how each design allocates memory than about how much memory it uses at the end. A pre-sized order array would remove most of C's page faults.

### 11.5 Production choice

If I could deploy only one, I would pick **C**.

- **Latency and predictability:** C has the lowest average and the lowest p99 for add and cancel (86 ns and 297 ns, against 432 and 906 for A and 1,239 and 1,489 for B). For a trading system I care about the tail as much as the average.
- **Cache behavior:** its working set is small and accessed by computed address, which is why it has the best IPC (1.46 against about 0.75) and the fewest misses.
- **Memory usage:** the price levels take a fixed 80 KB that never grows. The order vector does grow, and C never frees finished orders, which B does not either. A's memory follows the number of live orders, which is better for a very long run.
- **Complexity and maintainability:** C is short and has no tricky library behavior, but it has the most hidden assumptions: a bounded price band, dense increasing IDs, and a scan that is O(P) in the worst case. A is the easiest to read and has the fewest assumptions, but its latency is less predictable because of node allocation and pointer chasing. B has the worst tail and the most intricate code (two orderings and shifting inserts).

C's assumptions are fine for this assignment because the price range and IDs are given. In a real system I would replace the ID-indexed vector with something that does not need dense IDs, and I would validate prices on input before using them as indexes.

### 11.6 What I would try next

- **Bitset for the best price:** keep one bit per level and use a count-trailing-zeros instruction to find the next non-empty level, instead of scanning an array. SIMD or AVX scanning could speed this up further.
- **Pre-allocate the order store:** reserve the order vector once up front. This should remove the repeated growth in C and its extra page faults.
- **Object pool or custom allocator for A:** allocate map and hash nodes from a pool so they sit close together and malloc is not called per order.
- **Better order-ID lookup:** replace C's ID-indexed vector with an open-addressing hash table or a slot map so IDs do not have to be dense, and so finished orders can be reused.
- **Less timer overhead:** time batches of operations, or use a cheaper cycle counter, so the BBO numbers are not dominated by the clock read.
- **Better layout for B:** store prices and quantities in separate arrays so a binary search reads fewer cache lines.
- **Make the benchmark steadier:** fix the CPU frequency and close other programs, so that `perf stat` and the latencies can come from the same session.
