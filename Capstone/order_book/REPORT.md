# Report: Three Order Book Designs

GitHub: Divii2205, SST email: divijaa.24bcs10206@sst.scaler.com, Roll number: 24BCS10206

## Setup

- Machine for the timings: MacBook with Apple Silicon (arm64), macOS
- Compiler: `g++ -O2 -std=c++20` (on macOS this is Apple clang 21)
- Workload: 1,000,000 ops, seed 42, about 70% add, 20% cancel, 10% BBO, plus `get_bids(10)` and `get_asks(10)` every 100 ops
- Each implementation ran 5 times and the median is reported
- Full output: `results/bench_mac.txt`, `results/verify_output.txt`, `results/mac_time_stats.txt`

`perf` is not available on macOS. The rows marked **(Linux perf)** have to be filled from `./run_perf.sh` on a Linux machine (the files go to `results/perf_stat_A.txt`, `_B.txt`, `_C.txt`). Until then I used `/usr/bin/time -l` on the Mac, which gives real instruction and cycle counts (so IPC) and page reclaims. Those counters cover the whole program, including making the workload, which is the same work for all three.

## Results Table

| Metric | Implementation A (map) | Implementation B (heap) | Implementation C (array) |
| ----- | ----- | ----- | ----- |
| Total time (ms) | 338.54 | 153.88 | 69.32 |
| ns / add | 209.3 | 87.4 | 31.6 |
| ns / cancel | 747.9 | 191.2 | 53.8 |
| ns / BBO | 28.7 | 27.8 | 28.5 |
| ns / depth query (bids + asks) | 561.7 | 2334.5 | 469.2 |
| p99 add / cancel (ns) | 334 / 1875 | 250 / 667 | 42 / 333 |
| IPC (Mac, time -l) | 1.82 | 2.75 | 2.75 |
| Instructions (Mac) | 1,601,309,103 | 1,518,015,053 | 859,778,705 |
| Cycles (Mac) | 878,899,211 | 551,197,414 | 312,657,361 |
| IPC (Linux perf) | TODO | TODO | TODO |
| Branch misses (%) (Linux perf) | TODO | TODO | TODO |
| L1-dcache-load-misses (Linux perf) | TODO | TODO | TODO |
| Cache misses (Linux perf) | TODO | TODO | TODO |
| dTLB-load-misses (Linux perf) | TODO | TODO | TODO |
| Page faults (Linux perf) | TODO | TODO | TODO |
| Page reclaims (Mac, minor faults) | 4192 | 4550 | 3224 |
| Max memory (Mac) | 66.3 MB | 72.2 MB | 50.5 MB |

Counters that were not available: on the Mac there is no way to read branch misses, cache misses, L1 or TLB misses without perf. If some counters show `<not supported>` on Linux (this often happens inside WSL2 or a VM), I will write which ones here.

**A note on the timer.** Two `steady_clock::now()` calls back to back take about 27 ns on my Mac, and every timed op includes this. All the p99 values are multiples of about 41.7 ns (42, 250, 333, 667, 1875), so it looks like the clock on my Mac only moves in steps of about 41.7 ns. This means the BBO numbers (about 28 ns for all three) are mostly the cost of the timer itself, and the real BBO cost is a few ns. For BBO the per op numbers can not tell the three designs apart. The total time and the add and cancel numbers are big enough to trust.

## 11.1 Which implementation is fastest?

- **add_order**: C, 31.6 ns, about 7 times faster than A and 3 times faster than B.
- **cancel_order**: C, 53.8 ns, about 14 times faster than A and 3.5 times faster than B.
- **get_bbo**: all three look the same (about 28 ns) because of the timer note above. In terms of work, A reads `begin()` of two maps, B reads two heap tops and does two hash lookups, and C reads two cached indexes and two array slots, so C does the least work.
- **Depth queries**: C is fastest, A is close, and B is about 4 to 5 times slower because a heap only knows its top, so `get_bids` has to copy the whole heap and pop from it.

Why C is fastest: in C the price is the index, and the order id is the index. There is no search at all. An add is two array writes and one compare. A cancel is one array read, one subtract, and in most cases nothing else. A has to walk a tree with about 20 levels (500,000 live orders) to find an order, and then walk the level tree, and every step is a pointer to some other place on the heap. B uses hash maps so the lookup is O(1), but each lookup still hashes, jumps to a bucket and follows a pointer to the node, and a new price also pushes into a heap.

C also runs about half as many instructions as A and B (860 million vs about 1.5 to 1.6 billion for the whole program). A and B run about the same number of instructions, but A needs 60% more cycles, so A spends a lot of time waiting on memory. That is why its IPC is 1.82 and B and C are at 2.75.

## 11.2 Branch Prediction

**(fill the numbers from Linux perf, the reasoning below is what I expect from the code)**

I expect **A** to have the most branch misses. Every `find`, insert and erase in a red black tree goes down the tree, and at every node the CPU has to guess "go left or go right". The prices and ids in the workload are random, so these guesses are close to a coin flip. Insert and erase also do rebalancing with more branches that depend on the colors of the nodes.

B has the same kind of problem in the heap. When a tick is pushed it moves up, and when a dead tick is popped the last element moves down, and each step is a compare with a random result. Hash map lookups also have a branch for "is this the right key or go to the next node".

C has very few branches in the hot path. In `add_order` the compare `i > best_.bid` is almost always false once the book has orders, so it is easy to predict. In `cancel_order` the check `levels_[i] != 0` is almost always true because each level has many orders, and I marked it `[[likely]]`. The scan loop only runs when the best level becomes empty, which is rare.

## 11.3 Cache Behavior

**(fill the numbers from Linux perf)**

I expect **A** to have the most cache misses, and the Mac numbers already point there (lowest IPC, most cycles for about the same instructions as B).

- **A** follows pointers all the time. Every map node is a separate heap allocation, so nodes that are next to each other in the tree can be on different cache lines and even different pages. The order map has 500,000 nodes by the end, which is far too big for L1 or L2, so most steps down the tree are a cache miss. This is the non contiguous memory problem sir talked about with the sorted set.
- **B** keeps the heaps in a vector, which is flat, but the two hash maps are not. `std::unordered_map` stores each entry as a separate node, and the bucket array points to them. So a cancel touches the order bucket, the order node, the level bucket and the level node, which are all in different places. This is the same "two cache lines for every operation" issue we discussed for the priority queue with a hash map for lazy deletion. The order map also has to rehash many times as it grows.
- **C** uses contiguous memory. The level array is about 80 KB, so most of it can stay in L2, and the best levels near the mid are touched all the time so they stay hot. The order pool is one array, so finding an order is one address calculation. A cancel of an old order is still one cache miss into the pool (the pool is 16 MB and the order is random), but it is only one miss instead of a chain of them. The depth walk reads neighbor slots, which are on the same cache line, so it is cheap even when it skips empty slots. Also, because bids and asks share one array around the mid, when the book is full the best bid (99.99, slot 4999) and best ask (100.01, slot 5001) are only 16 bytes apart, so `get_bbo` usually reads one cache line for both.

Allocating many small objects (A and B) is also slow by itself, because each `new` has to go to the allocator, and it spreads the data out in memory.

## 11.4 Page Faults

**(fill the numbers from Linux perf)**

On the Mac, B had the most page reclaims (4550) and used the most memory (72 MB), then A (4192, 66 MB), then C (3224, 50 MB). Part of these numbers is the same for all three: the workload list (about 24 MB) and the vectors I use to store every op time for the p99 numbers.

- B has two hash maps with one node per entry, plus bucket arrays. When a hash map grows it allocates a new bucket array and moves everything, and the old one is freed, so new pages keep being touched.
- A has one node per order and per level. Each node has three pointers, a color and the data, so a node is about 48 to 64 bytes for a 16 byte order. That is a lot of memory for a little data.
- C touches its pages once at the start. The pool is 16 MB and it is zeroed in the constructor, so all of its page faults happen before the first order, and none in the middle of trading. The level array is only 80 KB.

This tells me A and B keep asking the OS for new memory while they run, and a page fault in the middle of a trade is a big latency spike. C pays the cost up front.

## 11.5 Production Choice

I would choose **C**, but not only because its average is the lowest.

- **Latency**: lowest for add and cancel, and lowest for depth queries.
- **Predictability**: this matters more than the average. The p99 cancel is 333 ns for C, 667 ns for B and 1875 ns for A. C does not allocate memory while running, so there are no surprise allocator calls, rehashes or page faults in the hot path. The one slow case is the scan when the best level empties and the next level is far away. With a busy book the next level is usually the next slot, and the worst case is bounded (5000 slots, all in one 40 KB block on that side).
- **Memory usage**: fixed and known up front. 80 KB for levels and 16 bytes per order id. A and B grow while running and use more memory per order.
- **Cache behavior**: contiguous arrays, best levels near each other, one miss per cancel at most in normal cases.
- **Complexity**: the code is the shortest of the three. The only tricky part is keeping `best_.bid` and `best_.ask` right, and the `--verify` test covers it.
- **Maintainability**: easy to read. The main limit is that it depends on the fixed price band and on ids being small numbers that go up. If the price band could move a lot during the day, or ids came from many sources, I would need to re center the array or add a hash map from id to pool slot. A is the easiest to change for those cases, so A would be my choice for a quick prototype, but not for the real system.

## 11.6 What Would I Try Next?

- **Bitmap of non empty levels.** Keep one bit per price slot. Then finding the next non empty level after a cancel, and walking the depth, can skip 64 empty slots at once with a count leading zeros instruction instead of checking slot by slot.
- **Smaller level slots.** Total qty fits in 32 bits for this workload, so `int32_t` levels would make the array 40 KB and fit more levels per cache line.
- **Hash map from id to pool slot** with open addressing (a flat array, no nodes), so C works with any id and still has no pointer chasing.
- **Move the array with the market.** Right now the array covers the whole fixed band. If the band was much wider, I would keep a smaller window around the mid and shift it when the price moves.
- **Pre allocate for A and B.** `reserve` the hash maps in B and use a pool allocator for the map nodes in A, to see how much of their cost is allocation and how much is the structure itself.
- **Branchless binary search.** For a sorted vector of levels (another design we discussed), try the branchless version and compare branch misses with perf.
- **Better timing.** Time batches of ops instead of each op, or read the CPU counter directly, so the 27 ns timer cost and the 41.7 ns clock step do not hide the BBO cost.
- **Price time priority.** Keep a FIFO list of orders per level (with the pool, an intrusive linked list by index) so the book is ready for matching later.
