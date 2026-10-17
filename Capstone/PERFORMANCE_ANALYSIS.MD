
# Performance Analysis & Hardware Profiling Report


**Author:** Rachit S   
**SST Email:** rachit.24bcs10139@sst.scaler.com  
**Roll Number:** 24bcs10139    

---

## 1. Correctness Verification Output

Before benchmarking, all three implementations were verified in lockstep using `./bench --verify`:

```text
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.

```

Furthermore, across all 1,000,000 operations of the benchmark workload (`std::mt19937 rng(42)`), all three implementations produced the exact same final book state and checksum:

* **Final Best Bid:** `99.99 x 2722`
* **Final Best Ask:** `100.01 x 2525`
* **Checksum:** `267280187`

---

## 2. Completed Results Table (Section 10)

All latencies below represent the **median of 5 runs** measured via `std::chrono::steady_clock` on Linux (WSL2 Ubuntu), paired with hardware PMU counters captured via `perf stat -r 5`:

| Metric | Implementation A (`OrderBookA`) | Implementation B (`OrderBookB`) | Implementation C (`OrderBookC`) |
| --- | --- | --- | --- |
| **Total time (ms)** | `320.71 ms` | `250.24 ms` | **`75.45 ms`** |
| **ns / add** | `276.60 ns` | `139.39 ns` | **`33.46 ns`** |
| **ns / cancel** | `465.27 ns` | `584.75 ns` | **`114.40 ns`** |
| **ns / BBO** | `22.84 ns` | `22.47 ns` | **`21.98 ns`** |
| **IPC (Instructions / Cycle)** | `0.62` | `0.69` | **`1.18`** |
| **Branch misses (%)** | `5.45%` (`42,947,013`) | `9.50%` (`50,300,965`) | **`1.66%` (`5,539,709`)** |
| **L1-dcache-load-misses** | `97,787,164` | `172,260,505` | **`11,498,638`** |
| **Cache misses (LLC)** | `52,443,406` | `103,577,688` | **`12,061,978`** |
| **dTLB-load-misses** | `4,411,197` | `6,355,000` | **`430,164`** |
| **Page faults** | `19,699` | `21,889` | `22,622` |

---

## 3. Raw `perf stat` Outputs (Section 8)

### 3.1 Implementation A (`std::map` + `std::unordered_map`)

```text
Performance counter stats for './bench --impl A' (5 runs):

           1871.88 msec task-clock:u                                                  ( +-  0.33% )
        6055903005      cycles:u                                                      ( +-  0.24% )  (71.29%)
        3750372752      instructions:u                                                ( +-  0.10% )  (85.63%)
         788704666      branches:u                                                    ( +-  0.15% )  (85.63%)
          42947013      branch-misses:u                                               ( +-  0.29% )  (85.65%)
          52443406      cache-misses:u                                                ( +-  0.79% )  (85.73%)
          97787164      L1-dcache-load-misses:u                                       ( +-  0.21% )  (85.87%)
           4411197      dTLB-load-misses:u                                            ( +-  3.98% )  (57.12%)
             19699      page-faults:u                                                 ( +-  0.00% )

       1.881745391 +- 0.007006424 seconds time elapsed  ( +-  0.37% )

```

### 3.2 Implementation B (Sorted `std::vector` + Binary Search)

```text
Performance counter stats for './bench --impl B' (5 runs):

           1368.80 msec task-clock:u                                                  ( +-  1.70% )
        4316607307      cycles:u                                                      ( +-  1.42% )  (71.45%)
        2987456038      instructions:u                                                ( +-  0.13% )  (85.72%)
         529360836      branches:u                                                    ( +-  0.12% )  (85.71%)
          50300965      branch-misses:u                                               ( +-  0.11% )  (85.72%)
         103577688      cache-misses:u                                                ( +-  1.32% )  (85.71%)
         172260505      L1-dcache-load-misses:u                                       ( +-  0.20% )  (85.70%)
           6355000      dTLB-load-misses:u                                            ( +-  4.79% )  (57.15%)
             21889      page-faults:u                                                 ( +-  0.00% )

       1.376811149 +- 0.022810205 seconds time elapsed  ( +-  1.66% )

```

### 3.3 Implementation C (Direct Tick-Indexed Array + Cached BBO)

```text
Performance counter stats for './bench --impl C' (5 runs):

            497.29 msec task-clock:u                                                  ( +-  3.79% )
        1475210061      cycles:u                                                      ( +-  3.45% )  (71.19%)
        1734182550      instructions:u                                                ( +-  0.63% )  (85.85%)
         333216996      branches:u                                                    ( +-  0.17% )  (85.85%)
           5539709      branch-misses:u                                               ( +-  3.38% )  (85.86%)
          12061978      cache-misses:u                                                ( +-  2.81% )  (85.85%)
          11498638      L1-dcache-load-misses:u                                       ( +-  0.50% )  (85.84%)
            430164      dTLB-load-misses:u                                            ( +-  3.48% )  (56.60%)
             22622      page-faults:u                                                 ( +-  0.00% )

       0.504228971 +- 0.018910775 seconds time elapsed  ( +-  3.75% )

```


## 4.Performance Analysis (Section 11)

### 11.1 Which Implementation Is Fastest?

**Implementation C (`OrderBookC`)** is decisively the fastest implementation overall (`75.45 ms` total median execution time, compared to `250.24 ms` for Implementation B and `320.71 ms` for Implementation A), winning across every single operation type:

1. **`add_order` (`33.46 ns` in C vs. `139.39 ns` in B vs. `276.60 ns` in A):**
* **Implementation C** is **8.26x faster** than A and **4.16x faster** than B. Adding an order in C requires zero dynamic allocations, zero pointer chasing, and zero search loops: it computes the array index via subtraction (`tick - 5000`), adds `qty` directly to `bid_qty[idx]`, writes the order into pre-allocated `order_table[id]`, and updates `best_bid_idx` with a single comparison.
* **Implementation B** beats A by **1.98x** on `add_order` because incoming Order IDs are strictly increasing, making `orders.push_back()` an $O(1)$ append at the tail of a pre-reserved contiguous vector, while `bids`/`asks` fit in CPU cache (`<= 5,000` levels).
* **Implementation A** is slowest (`276.60 ns`) because inserting into `std::map` and `std::unordered_map` allocates heap nodes via `operator new`, traverses non-contiguous Red-Black Tree nodes in $O(\log M)$, and rebalances tree pointers.


2. **`cancel_order` (`114.40 ns` in C vs. `465.27 ns` in A vs. `584.75 ns` in B):**
* **Implementation C** is **4.07x faster** than A and **5.11x faster** than B. It locates the order in $O(1)$ time with a single array lookup (`order_table[id]`), sets `active = false`, and decrements the tick array in $O(1)$.
* Interestingly, **Implementation A (`465.27 ns`) beats Implementation B (`584.75 ns`) on `cancel_order**`. Why? In Implementation A, looking up the order by ID in `std::unordered_map` is $O(1)$ average time (a single hash bucket lookup). In Implementation B, `cancel_order` must execute a binary search (`std::lower_bound`) over the massive `orders` vector containing up to **700,000 `OrderEntry` structs** (~16.8 MB of memory). Each of the ~20 binary search steps jumps to a distant memory address outside L1/L2 cache, causing severe cache and branch misses before it even searches the price vector.


3. **`get_bbo` (`21.98 ns` in C vs. `22.47 ns` in B vs. `22.84 ns` in A):**
* All three implementations achieve $O(1)$ asymptotic complexity for `get_bbo()` (`.begin()` in A, `.back()` in B, cached indices in C). Because two calls to `std::chrono::steady_clock::now()` incur a baseline measurement overhead of ~20 ns, all three cluster around `22–23 ns`, with **Implementation C** edging out B and A because `best_bid_idx` and `best_ask_idx` sit in a single `alignas(64)` L1 cache line.


4. **Instruction Efficiency (IPC):**
* Across 5 runs, Implementation C executes only `1.73 billion` instructions in `1.47 billion` cycles (**IPC = `1.18**`), whereas Implementation A executes `3.75 billion` instructions in `6.06 billion` cycles (**IPC = `0.62**`) and Implementation B achieves **IPC = `0.69**`. Implementation C's CPU pipeline operates nearly **twice as efficiently** per clock cycle because it is not stalled waiting on RAM fetches or branch mispredictions.



---

### 11.2 Branch Prediction

**Implementation B** has the highest branch-miss rate (`9.50%`, or `50,300,965` missed branches out of `529,360,836`), followed closely by **Implementation A** (`5.45%`, or `42,947,013` missed branches out of `788,704,666`). **Implementation C** has by far the lowest branch-miss rate at just **`1.66%`** (`5,539,709` misses—nearly **10x fewer** missed branches than B and **8x fewer** than A).

* **What causes the CPU to mispredict branches in B and A?**
Modern CPUs use an instruction pipeline that speculatively fetches and decodes instructions ahead of conditional jumps (`if/else`). When the branch predictor guesses wrong, the entire pipeline must be flushed, costing 15–20+ clock cycles.
* In **Implementation B**, every `add_order` performs a binary search (`std::lower_bound`) over `bids`/`asks`, and every `cancel_order` performs **two** binary searches ( ~20 steps over the `700,000`-element `orders` vector and ~12 steps over `bids`/`asks`). Inside `std::lower_bound`, the condition `if (mid < target)` depends on randomly chosen order IDs and uniformly distributed random prices. To the hardware branch predictor, every step of a binary search is a **50/50 coin flip** with zero historical pattern, causing massive branch mispredictions (`50.3M`).
* In **Implementation A**, walking down the Red-Black Tree (`std::map`) during `add_order` and `cancel_order` evaluates `if (tick < node->tick)` at each tree level (~12 hops). Because random prices go left or right with 50% probability, the branch predictor repeatedly mispredicts the tree path (`42.9M` misses).
* In **Implementation C**, binary search and tree traversal are completely eliminated in favor of **direct arithmetic indexing** (`idx = tick - MIN_BID_TICK` and `order_table[id]`). The only branches in C's hot path are the side check (`side == Side::Buy`) and the BBO cursor check (`idx > best_bid_idx`, which is almost always `false` once the best bid settles at `99.99`, making it >99.9% predictable).



---

### 11.3 Cache Behavior

Surprisingly at first glance—but completely logical once inspected—**Implementation B** records the highest cache misses (`172.26M` L1 load misses, `103.58M` LLC misses), followed by **Implementation A** (`97.79M` L1 load misses, `52.44M` LLC misses), while **Implementation C** slashes cache misses by an order of magnitude (**`11.50M` L1 load misses, `12.06M` LLC misses**).

Let us analyze how each implementation interacts with the 64-byte CPU cache line hierarchy:

1. **Implementation A (Pointer Chasing & Small Object Allocations):**
`std::map` and `std::unordered_map` allocate a separate heap object via `operator new` for every price level and order. Each Red-Black Tree node holds three 8-byte pointers (`parent`, `left`, `right`), color metadata, key, and value. Traversing `std::map` follows pointers across scattered heap addresses (`97.8M` L1 misses and `52.4M` LLC misses). Every pointer hop fetches a 64-byte cache line (~100 ns from RAM) just to inspect one node, wasting the remaining spatial locality of the cache line and starving the CPU core (IPC drops to `0.62`).
2. **Why Implementation B Had Even More Cache Misses Than A (`172.3M` L1 / `103.6M` LLC):**
While `OrderBookB` uses contiguous `std::vector` containers, look at what happens during `cancel_order(id)`:
* In our workload, `700,000` orders are added per run. At `24 bytes` per `OrderEntry`, the `orders` vector spans **~16.8 MB** of RAM—far larger than the CPU's L1 (`32–48 KB`) and L2 (`512 KB – 1 MB`) caches, and pushing the limits of L3 cache.
* Performing `std::lower_bound` binary search over a 16.8 MB array on every single cancel (`200,000` cancels per run $\times 5$ runs = `1,000,000` binary searches) probes index `N/2`, then `N/4`, then `3N/8`—**jumping wildly across megabytes of memory** on the first 10–12 probes of every search! Each probe fetches a cold 64-byte cache line, uses only 1 element (`24 bytes`), and discards the rest.
* Additionally, whenever a new price level is inserted into `bids` or `asks` (or erased when quantity drops to `0`), `std::vector::insert` / `erase` shifts all subsequent elements via `memmove`, streaming through multiple cache lines.


3. **Why Implementation C Dominates Cache Locality (`11.5M` L1 / `12.1M` LLC):**
* **L2-Resident Price Book:** `bid_qty` and `ask_qty` are fixed arrays of `5,000` `long`s (`40 KB` each = `80 KB` total). The entire 10,000-tick price book fits comfortably inside the CPU's **L1/L2 cache** and stays hot for the entire benchmark.
* **Single-Touch Order Lookup:** Instead of ~20 binary-search memory jumps across a 16.8 MB vector (as in B) or hash-bucket pointer chasing (as in A), `cancel_order(id)` in C performs **exactly one** memory access (`order_table[id]`). That reduces L1 cache misses by **15x** compared to B and **8.5x** compared to A!



---

### 11.4 Page Faults & TLB Behavior

* **Page Faults (`19,699` in A vs. `21,889` in B vs. `22,622` in C):**
All three implementations record roughly `~20,000–22,600` minor page faults across the 5-run suite (~4,000–4,500 page faults per run). On Linux, virtual memory is managed in **4 KB (`4,096`-byte) pages**. When a container allocates a large buffer on the heap (via `mmap` / `brk`), the OS kernel maps virtual pages lazily and only wires physical RAM frames on the first write (**demand paging / minor page fault**).
* In **Implementation C**, each run constructs an `OrderBookC` with `1,000,005` `DirectOrderSlot` structs (`16 bytes` each $\approx 16\text{ MB}$). Divided by `4,096 bytes/page`, touching that 16 MB table across 5 runs faults in `~4,000 pages/run` $\times 5 = 20,000$ minor page faults deterministically.
* In **Implementation B**, `orders.reserve(750'000)` allocates `750,000 * 24 bytes` $\approx 18\text{ MB}$ per run, producing almost the exact same number of demand-paged minor faults (`21,889`).
* In **Implementation A**, page faults (`19,699`) occur incrementally inside `operator new` as `std::map` and `std::unordered_map` request new heap pages from the kernel allocator during the benchmark loop.


* **Data TLB Misses (`dTLB-load-misses`: `430,164` in C vs. `4,411,197` in A vs. `6,355,000` in B):**
The **dTLB (Data Translation Lookaside Buffer)** caches virtual-to-physical page translations so the CPU does not have to walk the multi-level page table in RAM on every memory load.
* **Implementation C** has **10x fewer dTLB misses than A** and **14.7x fewer dTLB misses than B** (`430K` vs. `4.41M` and `6.36M`). Because C updates price levels inside a compact `80 KB` array (`20` 4 KB pages, which stay permanently pinned in L1 dTLB) and touches `order_table[id]` only once per add/cancel, it barely stresses the TLB.
* By contrast, **Implementation A** chases pointers across thousands of fragmented heap pages, and **Implementation B**'s binary search jumps across `~4,200` different 4 KB pages on every `cancel_order`, constantly thrashing the TLB!



---

### 11.5 Production Choice

If deploying one implementation to a live high-frequency trading production environment, I would unequivocally choose **Implementation C (`OrderBookC`)** (paired with a pre-allocated power-of-two flat hash table or ring buffer if exchange Order IDs are sparse 64-bit numbers rather than sequential).

1. **Latency & Tail-Latency Predictability (P99.9):**
In HFT, deterministic worst-case latency (P99/P99.9) matters far more than average speed. Implementation A invokes `new` and `delete` on the critical path, which can trigger allocator lock contention, freelist searches, or kernel page faults at unpredictable moments. Implementation B has $O(M)$ worst-case insertion/deletion shifts (`memmove`) and $O(\log N)$ binary search stalls that degrade as order volume grows. Implementation C performs `add_order`, `cancel_order`, and `get_bbo` in constant $O(1)$ time with zero dynamic memory allocations.
2. **Memory Usage & Predictable Footprint:**
Implementation C's price ladder occupies a fixed `80 KB` footprint (`2 x 5,000 x 8 bytes`) known at compile time. Unlike `std::map` (which wastes 32+ bytes of pointer/allocator overhead per node), C stores zero pointers and never fragments the heap.
3. **Cache & TLB Hygiene:**
With `1.18` IPC, `1.66%` branch misses, and `10x–15x` fewer L1 and dTLB misses than A and B, Implementation C leaves the CPU's L1/L2 caches warm for the trading strategy logic running alongside the book.
4. **Complexity & Maintainability:**
Implementation C is actually the simplest of the three designs to maintain and audit: tick-to-index mapping is pure linear arithmetic (`tick - MIN_BID_TICK`), completely free of iterator invalidation bugs or complex comparator lambdas.

---

### 11.6 What Would You Try Next?

To push Implementation even further in a next-generation production iteration, I would investigate five targeted low-latency optimizations:

### 11.6 What Would You Try Next?

To evolve this order book into a complete, sub-microsecond production trading engine, I would investigate the following next-stage optimizations:

1. **Fixed-Size Freelist `MemoryPool` + Open-Addressed Flat Hash Table for Order Lookup:**  
   Our current `order_table` relies on sequential Order IDs (`1..1,000,000`) to index directly into a vector. In live markets, exchanges assign sparse 64-bit Order IDs (such as nanosecond timestamps or UUIDs) that cannot be used as raw array indices without exhausting RAM. To handle arbitrary 64-bit IDs with zero runtime heap allocations, I would pair a pre-allocated freelist `MemoryPool<OrderSlot, PoolSize>` (managing object lifetimes via placement `new` without ever calling `new`/`delete` on the hot path) with a power-of-two open-addressed flat hash table using bitwise masking (`idx = hash(id) & (Capacity - 1)` instead of slow modulo division).

2. **Multi-Threaded Lock-Free SPSC Ring Buffer Pipeline:**  
   While this benchmark is single-threaded, a live HFT architecture separates IO-bound network ingestion from CPU-bound book updates using a three-thread pipeline: a market-data listener thread, an order-book/strategy engine thread, and an order-sender thread. I would connect these stages using a lock-free Single-Producer Single-Consumer (SPSC) ring buffer backed by raw pre-allocated storage (`::operator new`), power-of-two bitmask indexing, `std::memory_order_acquire` / `std::memory_order_release` half-fences, and `alignas(64)` on the producer `tail_` and consumer `head_` indices to eliminate MESI cache-coherence false sharing across cores.

3. **CPU Core Pinning (`taskset`) & OS Scheduler Isolation:**  
   To prevent the Linux kernel scheduler from preempting the order book thread or migrating it across cores (which flushes L1/L2 caches and triggers TLB misses and minor page faults), I would pin each thread to a dedicated isolated physical CPU core using `taskset -c` (following the "one core, one thread, one job" principle).

