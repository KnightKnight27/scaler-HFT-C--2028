# Rohan Ranjan-10428-Capstone project: report

## Setup

- Machine: MacBook with Apple M4 (10 cores; performance core L1D 128 KB, L2 16 MB), macOS 26.5, 16 KB pages
- Compiler: Apple clang 17, `-std=c++20 -O2`
- perf: Linux in Docker Desktop on the same Mac (kernel 7.0.12-linuxkit, aarch64), g++ 12.2 `-O2 -g -fno-omit-frame-pointer`, perf 6.1
- cachegrind: valgrind 3.19 in the same Docker image, simulated caches set to the M4 performance core (L1 data 128 KB 8-way, last level 16 MB 16-way, 128-byte lines)
- Workload: 1,000,000 ops, seed 42, the same list for A, B and C. Every timing is the median of 5 runs.

One limitation up front. The Docker VM does not pass the CPU's hardware counters through, and macOS has no perf, so `cycles`, `instructions`, `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses` and `dTLB-load-misses` all came back `<not supported>` in `perf stat`. What I could measure: `task-clock` and `page-faults` with perf, a `perf record` profile (sampled with `cpu-clock`), and cycles + instructions (so IPC) from macOS itself via `/usr/bin/time -l` (`mac_counters.sh`).

For branch and cache misses I used valgrind's cachegrind (`cachegrind.sh`). It runs the program on a simulated CPU, so it works where the hardware counters are blocked. These numbers are simulated: cachegrind's branch predictor is much simpler than the M4's, so the real miss rates are lower, but A, B and C go through exactly the same model, which makes them fair to compare. The `none` run (workload only) is subtracted from every cachegrind number. Only `dTLB-load-misses` stayed unmeasured, because cachegrind doesn't model the TLB. Running `./run_perf.sh` on a lab Linux machine would give the real hardware counts.

## Results table

| Metric | Implementation A | Implementation B | Implementation C |
|---|---|---|---|
| Total time (ms) | 89.50 | 95.81 | **11.70** |
| ns / add | 86.3 (73.3 without timer) | 45.1 (32.2) | **16.3 (3.7)** |
| ns / cancel | 148.8 (135.8) | 275.4 (262.5) | **25.3 (12.7)** |
| ns / BBO, in the workload | 13.9 (0.9) | 14.2 (1.3) | 13.5 (0.9) |
| ns / BBO, 10M calls in a loop | 0.52 | 1.69 | 0.59 |
| ns / get_bids(10) + get_asks(10) | 87.9 | 609.0 | 206.3 |
| IPC (macOS counters) | 1.61 | 2.20 | **3.54** |
| Instructions per run (macOS) | 659.6 M | 891.0 M | 152.3 M |
| Branch misses (%) (cachegrind) | **9.73%** (8.78 M of 90.3 M) | 4.79% (6.33 M of 132.2 M) | 4.21% (0.94 M of 22.4 M) |
| L1-dcache misses (cachegrind, D1) | **9.01 M** | 6.50 M | 1.15 M |
| Cache misses (cachegrind, last level LLd) | **1.18 M** | 1.03 M | 0.61 M |
| dTLB-load-misses | not supported in VM | not supported in VM | not supported in VM |
| Page faults (perf, Linux, 4 KB pages) | 7,475 | 8,840 | **3,550** |
| Page faults (macOS, 16 KB pages, minus baseline) | 2,072 | 2,020 | 1,821 |
| Peak memory (macOS) | 67.7 MB | 66.9 MB | 63.8 MB |
| task-clock (perf, Linux) | 142.5 ms | 130.8 ms | 30.0 ms |

Notes on the numbers:

- Every per-call time includes one `steady_clock::now()` pair, about 13 ns here. The number in brackets subtracts that. On the M4 `steady_clock` only ticks every ~42 ns, so a single call of ~1 ns can't really be timed; the averages over 100k-700k calls are still fine, but for `get_bbo` I added the tight-loop measurement (10M calls, an empty `asm` barrier so the compiler can't reuse the result).
- The perf runs and the baseline include building the workload. `./bench --impl none` (workload only) used 17.3 ms task-clock and 960 page faults, so those should be subtracted when comparing. For IPC I subtracted the `none` run's cycles and instructions.

Single-call latency (ns, timer included, steps of ~42 ns), median of 5 runs:

| | A p50 / p99 / p99.9 / max | B p50 / p99 / p99.9 / max | C p50 / p99 / p99.9 / max |
|---|---|---|---|
| add_order | 83 / 125 / 291 / **5,287,833** | 42 / 84 / 167 / 29,292 | 0 / 42 / 42 / 329,834 |
| cancel_order | 125 / 375 / 500 / 11,167 | 208 / 833 / 1,084 / 31,042 | 0 / 84 / 167 / 2,667 |
| get_bbo | 0 / 42 / 42 / 250 | 0 / 42 / 42 / 417 | 0 / 42 / 42 / 6,000 |

## perf stat output

The commands are in `run_perf.sh`: `perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl X --total-only --runs 1`.

### Implementation A

```
 Performance counter stats for './bench --impl A --total-only --runs 1' (5 runs):

            142.54 msec task-clock                       #    0.978 CPUs utilized            ( +-  2.89% )
   <not supported>      cycles                                                      
   <not supported>      instructions                                                
   <not supported>      branches                                                    
   <not supported>      branch-misses                                               
   <not supported>      cache-misses                                                
   <not supported>      L1-dcache-load-misses                                       
   <not supported>      dTLB-load-misses                                            
              7475      page-faults                      #   51.895 K/sec                    ( +-  2.30% )

           0.14572 +- 0.00409 seconds time elapsed  ( +-  2.81% )
```

### Implementation B

```
 Performance counter stats for './bench --impl B --total-only --runs 1' (5 runs):

            130.80 msec task-clock                       #    1.034 CPUs utilized            ( +-  1.26% )
   <not supported>      cycles                                                      
   <not supported>      instructions                                                
   <not supported>      branches                                                    
   <not supported>      branch-misses                                               
   <not supported>      cache-misses                                                
   <not supported>      L1-dcache-load-misses                                       
   <not supported>      dTLB-load-misses                                            
              8840      page-faults                      #   70.816 K/sec                    ( +-  0.20% )

           0.12655 +- 0.00166 seconds time elapsed  ( +-  1.31% )
```

### Implementation C

```
 Performance counter stats for './bench --impl C --total-only --runs 1' (5 runs):

             29.94 msec task-clock                       #    0.915 CPUs utilized            ( +-  2.99% )
   <not supported>      cycles                                                      
   <not supported>      instructions                                                
   <not supported>      branches                                                    
   <not supported>      branch-misses                                               
   <not supported>      cache-misses                                                
   <not supported>      L1-dcache-load-misses                                       
   <not supported>      dTLB-load-misses                                            
              3550      page-faults                      #  110.485 K/sec                    ( +-  1.39% )

          0.032704 +- 0.000903 seconds time elapsed  ( +-  2.76% )
```

### Baseline (workload only, `--impl none`)

```
 Performance counter stats for './bench --impl none --total-only --runs 1' (5 runs):

             17.32 msec task-clock                       #    0.965 CPUs utilized            ( +-  3.47% )
   <not supported>      cycles                                                      
   <not supported>      instructions                                                
   <not supported>      branches                                                    
   <not supported>      branch-misses                                               
   <not supported>      cache-misses                                                
   <not supported>      L1-dcache-load-misses                                       
   <not supported>      dTLB-load-misses                                            
               960      page-faults                      #   54.125 K/sec                    ( +-  9.64% )

          0.017948 +- 0.000606 seconds time elapsed  ( +-  3.38% )
```

## cachegrind output

`./cachegrind.sh` (summary in `results/cachegrind/summary.txt`, full output per implementation next to it). Order book only, `none` subtracted:

```
order book only (impl minus none), simulated by cachegrind

impl           instr      data refs    D1 misses   LLd misses     branches  mispredicts  mispred%
A          515946585      217359452      9006295      1180951     90252308      8778679     9.73%
B          686989240      307260921      6495093      1030607    132232452      6330705     4.79%
C          138285613       53641502      1147534       610579     22356430       942077     4.21%
```

valgrind also prints a `brk segment overflow` note in each run. That only means `malloc` switched from `brk` to `mmap` for more memory; it doesn't change the counts.

## perf record

`perf record -g ./bench --impl X --total-only --runs 10`, then `perf report --no-children`. Top entries (long template names shortened):

| A | B | C |
|---|---|---|
| 47.3% `BookA::add_order` | 43.2% `BookB::cancel_order` | 26.8% `BookC::add_order` |
| 24.8% `BookA::cancel_order` | 12.9% `BookB::add_order` | 22.9% `run_total<BookC>` (cancel/bbo inlined here) |
| 4.5% `unordered_map::_M_rehash` | 6.2% `_Rb_tree_insert_and_rebalance` | 9.7% `vector<Slot>::_M_default_append` |
| 2.4% `run_total<BookA>` | 5.5% `map::_M_erase` | 5.8% kernel `clear_page` |
| 2.4% `free` + 1.8% `malloc` | 4.3% `_Rb_tree_rebalance_for_erase` | 3.7% `make_workload` |
| | 2.2% `free` + 2.1% `malloc` | 2.7% `BookC::cancel_order`, 1.5% `llround` |

Most of the map and hash-table code is inlined into `add_order` / `cancel_order`, which is why those two dominate A. The full reports are in `results/linux/perf_report_*.txt`.

## Performance analysis

### 11.1 Which implementation is fastest?

C is fastest for `add_order` (3.7 ns without the timer, against 32 ns for B and 73 ns for A) and for `cancel_order` (12.7 ns, against 136 ns for A and 262 ns for B). The whole workload takes 11.7 ms on C and about 90 ms on the other two, so roughly 8x.

For `get_bbo` all three are around a nanosecond, because all three keep the best price somewhere they can read straight away: `begin()` of a map is cached in the map header, the heap top is element 0, and C stores the best index. A and C tie at ~0.5 ns in the tight loop. B is slower (1.7 ns) since the heap only holds ticks, so it also has to look up the quantity in a hash map.

Why C wins on add and cancel: an add is an index calculation, two array writes and one bit set, with no allocation. The level array is 80 KB and the bitmap 1.3 KB, so both stay in the 128 KB L1 cache. New orders are written to the end of `by_id_` (ids go up by one), which is a sequential write the prefetcher handles well. A cancel is one random read of `by_id_[id]` (this is where C can miss the cache, since that vector is 16 MB) and then the same array work. A and B do much more on every call: walk a tree of ~12 levels (A's price map) or ~19 levels (B's order map, 500k orders), call `malloc` or `free` for a node, hash the key. The instruction counts show it as well: per run C executes 152 M instructions and A 660 M, and B 891 M.

B's add is faster than A's, which I did not expect. Most adds land on a price level that already exists, so B only touches a small hash map of at most 10,000 levels plus its order tree, where the new id is always the largest key, so the path to insert it is the same rightmost path every time and stays in cache. A has to search the price tree from the root on every add. Cancels flip it around: a cancel picks a random live id, and B's 500k-node tree search is about 19 dependent pointer loads to random places in memory, followed by a rebalance and a `free`. That is the 43% in B's profile, and it makes B's cancel the slowest of everything measured.

### 11.2 Branch prediction

A has the most branch misses: 8.78 M mispredicts, 9.7% of its branches, against 4.8% for B and 4.2% for C (cachegrind's simulated predictor).

The cause is the price tree. Every `add_order` in A searches the bid or ask `std::map` from the root, and at each of its ~12 levels the code compares the new price with the node's price and goes left or right. Prices are random, so that left/right branch is a coin flip and the predictor can't learn it. A cancel does the same search again (`find(tick)`), and inserts and erases add rebalancing branches whose direction depends on the tree's shape. With roughly 900k adds and cancels, 8.78 M mispredicts is about 10 per call, which is what a dozen coin-flip compares per search gives.

B runs more branches than anyone (132 M), but a smaller share go wrong. Its order tree is keyed by id, and new ids always go to the far right, so the insert path is the same every time and predicts well. Only cancels search B's tree for a random id (~19 levels), which is where most of its 6.33 M mispredicts come from. Its price-level lookup is a hash, which has very few branches.

C has the fewest branches overall (22 M, about a quarter of A's) and the lowest miss rate. Its hot path is straight-line array code. The branches it has are "was this level empty before" and "is this the new best price", and once the book has filled up those are almost always false, so they predict well. The bitmap search loop only runs when the best level empties.

### 11.3 Cache behavior

A also has the most cache misses: 9.01 M L1 data misses and 1.18 M last-level misses, against 6.50 M / 1.03 M for B and 1.15 M / 0.61 M for C. That is about 9 L1 misses per operation for A and about one for C.

A jumps around memory and follows pointers on every call. An add walks the price tree (nodes spread over the heap), hashes the id into the bucket array of a few MB at a random spot, and allocates a new hash node with `malloc`; a cancel does it all again. Both maps are built from many small objects (500k hash nodes by the end), each one a separate allocation, so nearly every step lands on a different cache line and has to wait for the previous load to come back.

B also follows pointers and allocates one tree node per order, but its common operation (an add at an existing price) touches a small hash table of at most 10,000 levels, which stays in cache, and the rightmost path of its order tree, which is the same few nodes every time. Its misses come from cancels walking the 500k-node tree to a random id. So B misses less than A even though it executes more instructions, and that is also why B's IPC (2.20) is higher than A's (1.61).

C mostly reads contiguous memory. The level array (80 KB) and the bitmap (1.3 KB) fit inside the 128 KB L1, adds write `by_id_` sequentially, and the one random access is the `by_id_[id]` read in cancel. Most of its 0.61 M last-level misses are probably that read plus the vector growth. Fewer misses and fewer branches together explain its IPC of 3.54.

### 11.4 Page faults

B has the most page faults on Linux (8,840), then A (7,475), then C (3,550); the workload alone causes 960. Each of B's and A's orders is a separate small allocation, so memory is touched a node at a time as the book grows to about 500k orders, and every new 4 KB page costs a fault. B's `std::map` node (three pointers, a color and the key/value) is bigger than A's hash node, which fits B being highest.

C's faults are mostly from one place: `by_id_` growing by doubling. The profile shows it directly (9.7% in `_M_default_append` and 5.8% in the kernel's `clear_page`). Each resize allocates a new block, zeroes it and copies the old one across. On macOS the gap is smaller (2,072 / 2,020 / 1,821) because pages are 16 KB, so each fault covers four times as much memory. Peak memory is about the same for all three (64-68 MB, about 35 MB of which is the workload). C is not smaller overall, because its `by_id_` keeps a 16-byte slot for every id ever used, cancelled or not.

### 11.5 Production choice

I would deploy C, with two changes first.

On latency it wins by a lot: add and cancel are 8-20x faster once the timer is subtracted, and its p99 stays at one or two timer ticks (42-84 ns with the timer) where A's cancel p99 is 375 ns and B's 833 ns. Predictability is where the max column helps. A has a single `add_order` that took 5.3 ms. It shows up in every run and `_M_rehash` is 4.5% of A's profile, so it is almost certainly the hash table rehashing all of its orders at once; in trading that is a disaster, because it happens exactly when the book is busiest. C's worst add is 330 µs, and that is the `by_id_` vector doubling. Reserving the vector at startup (for a day's worth of ids) removes that spike and most of C's page faults too, so that is the first change. B has no huge spike (worst ~30 µs) since a tree never moves everything at once, but it is slow on every cancel instead.

Memory is C's weak point. The price side is fixed and tiny (81 KB), but the order side grows with the number of ids ever issued and not with live orders, so it needs a reset each session or a way to recycle ids. The design also assumes the 10,000-tick band; a different instrument or a wider band means resizing the array. For cache behaviour C is clearly best: contiguous arrays and no per-order allocation.

On complexity and maintainability A wins. It is under 100 lines (comments included) that anyone can read, and I would keep it in the codebase as the reference to test C against, the same way `--verify` does now. C has more to get right (the bitmap search, the two cached indexes), but `--verify` and the planted-bug checks give me confidence in it.

### 11.6 What I would try next

- Reserve `by_id_` up front in C, and `orders_.reserve()` in A. Both latency spikes in the max column and a good share of the page faults come from growth.
- Use integer ticks in the API itself. `llround` from the double → tick conversion is 1.5% of C's profile, which is a lot for one conversion.
- For C, store the order slot as 8 bytes (32-bit qty + 16-bit tick) so twice as many fit per cache line.
- A pool allocator for A's and B's nodes, to replace `malloc` / `free` (about 4% of each profile) and keep nodes close together in memory.
- In B, look orders up in a hash map or by id like C; the tree lookup in cancel is its biggest cost.
- `get_bids` / `get_asks` allocate a new vector every call (C spends 206 ns on the pair, B 609 ns because of the heap walk). Filling a caller-provided array would avoid the allocation.
- If the book ever needs matching with price-time priority, each level in C would need a FIFO list of orders (an intrusive linked list through the order slots keeps cancel O(1)).
- Measure real hardware branch and cache misses (and dTLB misses) on a lab Linux box with `run_perf.sh`, to check the cachegrind numbers in 11.2 and 11.3.
