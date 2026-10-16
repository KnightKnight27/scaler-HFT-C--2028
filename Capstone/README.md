# Low-latency order book benchmark and analysis

Student: Prabal Patra  
Roll Number: `24BCS10031`  
SST Email: `prabal.24bcs10031@sst.scaler.com`  

## Project overview

I built a resting limit order book for one instrument three times, each on a different data structure, and benchmarked them on the same 1,000,000 operation workload.

- Tick size 0.01, bids in [50.00, 99.99], asks in [100.01, 150.00], mid 100.00
- 10,001 ticks from 50.00 to 150.00 (tick 0 is 50.00, tick 5000 is the unused mid)
- No matching, orders rest until cancelled
- Single threaded, standard library only, built with `-O2`

## Interface changes

The API is the one from the brief with two small changes:

- Method and field names are camelCase (`addOrder`, `cancelOrder`, `getBBO`, `getBids`, `getAsks`, `hasBid`, `bidPrice`, ...). Parameters and behaviour are the same.
- Prices are still `double` at the API, but every book converts them to an integer tick on the way in (`round((price - 50.00) * 100)`) and only converts back when returning a `Level` or `BBO`. No book uses a `double` as a key.

A price outside the 50.00 to 150.00 range is ignored by all three books, so they stay in agreement even on bad input.

## The three implementations

All three inherit from `OrderBook` in [`OrderBook/include/orderBook.hpp`](OrderBook/include/orderBook.hpp). The brief asks for the designs to differ in how they store orders, store levels, find the best price and find an order on cancel. This table is the short version:

| | A: `std::map` | B: heap + hash map | C: flat arrays |
| :--- | :--- | :--- | :--- |
| Orders stored in | `std::map<uint64_t, Order>` | `std::unordered_map<uint64_t, Order>` | `std::vector<Slot>` indexed by order id |
| Price levels stored in | two `std::map<int, long>` (bids descending, asks ascending) | two `std::unordered_map<int, LevelState>` | one `std::vector<long>` of 10,001 ticks |
| Best price from | `begin()` of each map | top of a max heap / min heap of ticks | two cached ints, `bestBidTick` and `bestAskTick` |
| Cancel finds the order by | tree lookup on id | hash lookup on id | direct index `slots[id]` |

### Implementation A: red-black trees everywhere

Orders live in a `std::map` keyed by id, levels in two `std::map`s keyed by tick. Best bid and ask are just `begin()`. A cancel walks the order tree to find the order, then walks the level tree, subtracts the quantity and erases the level node if it hit zero. Every new order and every new level is its own heap allocated node, which shows up later in the cache and page fault numbers.

### Implementation B: binary heaps with lazy deletion

Orders and per-level quantity are in hash maps. Each side also has a `priority_queue` of ticks. A tick is pushed only when its level goes from empty to non-empty, and a flag on the level (`inHeap`) makes sure a tick is never in the heap twice.

Cancel only subtracts from the level quantity. The heap is not touched, so a level can sit in the heap with quantity 0. `getBBO()` pops those stale ticks off the top until it finds a live one (that is why the heaps are `mutable`).

`getBids(depth)` / `getAsks(depth)` copy the heap and pop from the copy until they have `depth` live levels.

My first version of B did depth by copying every level out of the hash map and sorting it. That made the whole benchmark take about 1.77 s, almost all of it in the 20,000 depth calls, so I changed it to pop from a heap copy instead. B now runs in about 60 ms.

### Implementation C: price indexed arrays

Level quantity is a `std::vector<long>` with one slot per tick (about 80 KB). `bestBidTick` and `bestAskTick` are updated on add if the new order improves the inside. On cancel, if the best level drops to zero, the code walks tick by tick to the next non-empty level.

Orders are in a second vector, `slots`, indexed directly by order id. The benchmark ids go 1, 2, 3, ... so this is just an array lookup with no hashing. A slot has the tick, side, qty and a `live` flag, and cancel clears the flag. Ids at or above 2^24 go into a small `unordered_map` instead, so one huge id cannot make the vector allocate gigabytes. That path is tested in `--verify`.

### Complexity

L = number of active price levels, N = number of live orders, K = number of ticks in a heap (live plus stale), d = requested depth, g = number of empty ticks skipped while scanning.

| Operation | A | B | C |
| :--- | :---: | :---: | :---: |
| `addOrder` | O(log N + log L) | O(1) avg, plus O(log K) when the level is new | O(1) amortized |
| `cancelOrder` | O(log N + log L) | O(1) avg | O(1), or O(g) when the best level empties |
| `getBBO` | O(1) | O(1) amortized (stale pops are paid for by the cancels) | O(1) |
| `getBids(d)` / `getAsks(d)` | O(d) | O(K + d log K) for the heap copy | O(d + g) |

## Build and run

```bash
cd OrderBook
make clean && make          # or: cmake -B build && cmake --build build
```

<img width="1512" height="94" alt="Build output" src="https://github.com/user-attachments/assets/ac58f734-a8c2-4c9e-a69e-b5a6f414bf5e" />

```bash
./bench --verify            # correctness test
./bench                     # all three, 5 runs each, median printed
./bench --impl A            # one implementation (A, B or C), 5 runs
./bench --impl C --runs 1 --untimed   # no per-op clock reads, used for perf
./bench --gen-only          # only builds the workload, used as a perf baseline
```

## Correctness

`--verify` does two things.

1. Small edge cases run on each book on its own: empty book BBO and depth, cancel on an empty book, cancelling the same order twice, an unknown id, three bid levels when 10 are asked for, two orders at the same price being summed, the best bid moving down after its level empties, and a very large order id.
2. A random 100,000 operation workload (seed 1337) goes through all three books. After every single operation it compares the BBO and 10 levels of bids and asks across all three.

Output:

```
running correctness test...

edge cases (empty book, unknown and double cancel, short depth, large ids): PASS
random workload, 100000 ops, bbo + 10 levels checked after every op: PASS

implementation a: PASS
implementation b: PASS
implementation c: PASS

all implementations produce identical results.
```

<img width="1512" height="268" alt="Correctness test output" src="https://github.com/user-attachments/assets/6c96039d-52b7-4138-adef-118a64f1b94b" />

## Benchmark

1,000,000 operations from `std::mt19937(42)`: about 70% adds, 20% cancels of a random live order, 10% BBO queries, plus `getBids(10)` and `getAsks(10)` every 100 operations. Each add, cancel and BBO call is timed on its own with `std::chrono::steady_clock`. Every implementation runs 5 times on a fresh book and I report the run with the median total time. The checksum and final BBO are printed so the compiler can't drop the work.

Machine: my Mac (macOS, Apple clang, `-O2`).

<img width="1512" height="844" alt="Benchmark output, median of 5 runs" src="https://github.com/user-attachments/assets/e9987691-9d8a-460e-acda-25904912deb5" />

| Metric (median of 5) | A (`std::map`) | B (heap) | C (flat array) |
| :--- | :---: | :---: | :---: |
| Total time (ms) | 115.25 | 59.50 | 26.84 |
| ns / add | 67.75 | 29.60 | 13.73 |
| ns / cancel | 265.47 | 74.91 | 13.88 |
| ns / BBO | 12.38 | 14.57 | 12.27 |
| Total with `--untimed` (ms) | 110.0 | 47.4 | 6.5 |

Final BBO for all three: bid 99.99 x 2724, ask 100.01 x 2111, checksum 41255396894.

A's total moves around between runs on my machine (I have seen 115 to 150 ms), B and C are much more stable.

One thing I didn't expect: a single `steady_clock::now()` call costs about 14 ns here (measured with a separate 10 million call loop). Each timed op includes roughly one of those, so the ns numbers have a floor of about 12 to 14 ns. That is why all three BBO numbers look the same, and why C's add and cancel are mostly clock cost. The untimed row is the fairer comparison: C does the whole workload in 6.5 ms, about 6.5 ns per operation on average.

<img width="1512" height="290" alt="Untimed run of implementation C" src="https://github.com/user-attachments/assets/742823b6-4e61-4713-9da8-ac3038f615d9" />

## perf

### Setup and what was available

I don't have a Linux machine, so I ran `perf` 6.1 inside a Debian container (Docker Desktop on the Mac, kernel 7.0.14-linuxkit, g++ 12.2). The container VM does not expose the CPU's hardware counters, so only the software counters work:

```
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl C --runs 1 --untimed

 Performance counter stats for './bench --impl C --runs 1 --untimed' (5 runs):

             24.05 msec task-clock                       #    0.856 CPUs utilized            ( +- 12.65% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              2700      page-faults                      #   96.944 K/sec                    ( +-  0.01% )

           0.02811 +- 0.00307 seconds time elapsed  ( +- 10.91% )
```

The other three runs, same events:

```
 Performance counter stats for './bench --impl A --runs 1 --untimed' (5 runs):

            168.67 msec task-clock                       #    0.837 CPUs utilized            ( +-  9.87% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
             12091      page-faults                      #   60.498 K/sec                    ( +-  0.81% )

            0.2016 +- 0.0167 seconds time elapsed  ( +-  8.26% )
```

```
 Performance counter stats for './bench --impl B --runs 1 --untimed' (5 runs):

             83.36 msec task-clock                       #    0.877 CPUs utilized            ( +- 12.14% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              8546      page-faults                      #   90.975 K/sec                    ( +-  1.20% )

            0.0951 +- 0.0101 seconds time elapsed  ( +- 10.65% )
```

```
 Performance counter stats for './bench --gen-only' (5 runs):

             15.78 msec task-clock                       #    1.020 CPUs utilized            ( +-  1.00% )
   <not supported>      cycles
   <not supported>      instructions
   <not supported>      branches
   <not supported>      branch-misses
   <not supported>      cache-misses
   <not supported>      L1-dcache-load-misses
   <not supported>      dTLB-load-misses
              2172      page-faults                      #  142.142 K/sec                    ( +-  0.01% )

          0.015474 +- 0.000158 seconds time elapsed  ( +-  1.02% )
```

To fill the gaps I used two other sources, and the table below says where each number comes from:

- IPC: macOS's own counters from `/usr/bin/time -l` (instructions retired and cycles elapsed), native on my Mac, median of 5 runs. One run of each (whole process, so it still includes building the workload):

<img width="1512" height="636" alt="macOS counters for implementation A" src="https://github.com/user-attachments/assets/38e2ff76-7ef1-46b9-9136-a8f35e4f8fd1" />

<img width="1512" height="637" alt="macOS counters for implementation B" src="https://github.com/user-attachments/assets/b03d112e-efdb-4a0b-b284-664298c20726" />

<img width="1512" height="632" alt="macOS counters for implementation C" src="https://github.com/user-attachments/assets/16b4898e-cf9f-4779-8886-4ba18d26a7e1" />

- Cache and branch misses: Valgrind cachegrind with `--cache-sim=yes --branch-sim=yes` in the same container. These are simulated, not hardware counts, so I only use them to compare A, B and C against each other.

All counter runs use `--runs 1 --untimed` so the clock reads are not counted, and I subtracted a `--gen-only` run so only the order book work is left.

### Results table

| Metric (book only, baseline subtracted) | A | B | C | Source |
| :--- | :---: | :---: | :---: | :--- |
| task-clock (ms) | 152.9 | 67.6 | 8.3 | perf stat, Linux |
| page-faults | 9,919 | 6,374 | 528 | perf stat, Linux |
| instructions (M) | 762.4 | 721.5 | 118.9 | macOS counters |
| cycles (M) | 598.2 | 266.0 | 37.8 | macOS counters |
| IPC | 1.27 | 2.71 | 3.15 | macOS counters |
| branches (M) | 142.6 | 78.6 | 14.0 | cachegrind |
| branch mispredicts (M) | 12.51 | 2.30 | 0.89 | cachegrind |
| mispredict rate | 8.8% | 2.9% | 6.4% | cachegrind |
| L1 data misses (M) | 21.75 | 22.79 | 2.49 | cachegrind |
| last level data misses (M) | 9.94 | 7.64 | 1.66 | cachegrind |

### perf record

`perf record -e cpu-clock -g ./bench --impl X --runs 1 --untimed` (cpu-clock because there is no cycles event). Top symbols:

- A: `OrderBookMap::cancelOrder` 34%, `OrderBookMap::addOrder` 31%, plus about 14% in the red-black tree insert, erase and rebalance helpers. Almost all the time is tree work.
- B: spread out. Hash map `operator[]` and rehashing about 16%, `malloc` and `free` together about 29%, `memcpy` 5% (the heap copies in the depth queries) and `addOrder` 5%.
- C: the book barely shows up. Workload generation and the random number engine are over 50% of the samples, `OrderBookArray::addOrder` is 14%.

## Performance analysis

### Which implementation is fastest?

C, by a lot. Untimed it is about 17x faster than A and 7x faster than B over the whole workload. It runs 6x fewer instructions than A and B, and also gets more done per cycle (IPC 3.15 against 1.27 for A). An add in C is a tick conversion, one array increment, a slot write and a compare against the best tick. Nothing allocates and nothing is searched.

B is second. Its adds are cheap because most adds land on a level that is already in the heap, so it is just two hash map writes. It loses time in hashing, rehashing and allocation, and the depth queries copy a heap of up to about 5,000 ticks each time.

A is last and its cancel is the slowest operation in the whole benchmark (about 265 ns). Each cancel is two tree searches plus a possible erase and rebalance, and both trees are made of nodes scattered around the heap.

### Branch prediction

A has the most mispredicts by far, about 12.5 million for the book alone. Every step down a red-black tree is a compare on a random price or id, so the predictor is basically guessing. That fits the low IPC.

C actually has a higher mispredict rate than B (6.4% against 2.9%), but it runs so few branches that the total is the lowest: 0.89 million against 2.30 million. The branches that miss in C are mostly "does this add improve the best tick" and the end of the scan loop after a best level empties. Both depend on random prices.

### Cache behaviour

C's level array is 10,001 longs, about 80 KB, and the hot part near the inside of the book is much smaller than that. Neighbouring ticks share cache lines, so the scan after a cancel and the depth walk are both sequential reads. Bids and asks are in the same array instead of two separate ones, so when the book is tight the best bid (tick 4999) and the best ask (tick 5001) are only 16 bytes apart. `getBBO()` reads both from the same or the next cache line, instead of from two arrays somewhere else in memory. The order slots are also an array, and ids are handed out in order, so new orders are written next to each other. Cachegrind shows about 2.5 million L1 data misses for C against about 22 million for A and B.

A and B miss for different reasons. A follows tree pointers to nodes that were allocated at random times. B's hash maps spread entries over buckets, and the depth query copies the whole heap vector 20,000 times, which shows up as the `memcpy` in perf record and the high write misses in cachegrind.

### Page faults

A has the most page faults (about 9,900 over the baseline) because every order and every level is a separate heap node, so memory keeps growing as orders arrive. B is lower but still allocates a hash node per order. C touches only its two vectors. The level array is allocated once at startup, and the slot vector reserves space for 2^20 orders, so only about 500 extra faults show up. The macOS minor fault counts (page reclaims) follow the same order: +1,995 for A, +1,952 for B, +1,538 for C.

### What I would use in production

C, for this problem. The price band is fixed and small, so a flat array of levels is cheap in memory and very fast. Order ids that are handed out by the venue or the strategy are close to sequential, so a slot array indexed by id works well too. The cases where C gets worse are a very wide or unbounded price range (the array gets big and the scan after a cancel gets long) and sparse random order ids (that is what the overflow map is for, but it would need a real hash table at that point).

I would keep A around as a reference implementation in tests. It is the easiest one to read and check by eye.

### What I would try next

- A bitmap of non-empty ticks (157 words of 64 bits for 10,001 ticks), so finding the next best level after a cancel is `__builtin_clzll` / `__builtin_ctzll` on a few words instead of a loop.
- A free list in the slot array so ids can be reused once the venue recycles them.
- Pinning the benchmark to one core and averaging more runs, because A's numbers in particular move around between runs.
- Running perf on a real Linux machine to get hardware cache and branch counters instead of cachegrind's simulation.

## Note on checksums

The Linux runs print checksum 41255893145 instead of 41255396894. `std::uniform_int_distribution` is implemented differently in libstdc++ (Linux) and libc++ (macOS), so the same seed gives a slightly different workload on the two platforms. Within one platform all three books always print the same checksum and final BBO.
