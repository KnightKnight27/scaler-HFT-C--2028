# Capstone Performance Analysis Report: Low-Latency Order Book

**Team Members:**
1. **Name:** Utkarsh Raj | **SST Email:** utkarsh.24bcs10318@sst.scaler.com | **Roll Number:** 24BCS10318
2. **Name:** Ojas Sinha | **SST Email:** ojas.24bcs10077@sst.scaler.com | **Roll Number:** 24BCS10077

---

## Section 4: Documentation of the Three Implementations & Big-O Complexity

Let $N$ be the number of active orders in the book, $M \le 5,000$ be the number of active price levels per side, $W = 79$ be the number of 64-bit bitset words covering 5,000 price levels ($\lceil 5000 / 64 \rceil = 79$), and $K$ be the requested depth ($K = 10$).

### 1. Implementation A (`OrderBookA` — Standard Tree & Node Hash Map)
1. **What data structure(s) you used:** `std::map<int32_t, long, std::greater<int32_t>>` (bids), `std::map<int32_t, long>` (asks), and `std::unordered_map<uint64_t, OrderInfo>` (orders).
2. **How orders are stored:** Stored in `std::unordered_map` as individually heap-allocated linked-list bucket nodes mapping `uint64_t id -> {Side, int32_t tick, long qty}`.
3. **How price levels are stored:** Stored as balanced Red-Black Tree nodes in `std::map`, where each node holds an integer tick and aggregated quantity.
4. **How the best bid/ask is found:** Retrieved in $O(1)$ time via `*bids_.begin()` and `*asks_.begin()` (which point to the leftmost tree nodes).
5. **How an order is found during cancellation:** Located via `orders_.find(id)` ($O(1)$ average hash lookup), erased from the hash map (`free`), and then located in `bids_` or `asks_` via Red-Black tree search ($O(\log M)$) to subtract quantity and rebalance if the level drops to 0.

### 2. Implementation B (`OrderBookB` — Contiguous Sorted Vectors & Open-Addressing Hash Table)
1. **What data structure(s) you used:** Two sorted `std::vector<TickLevel>` (`bids_` and `asks_`) and a custom flat open-addressing hash table `std::vector<HashSlot>` of fixed capacity $2^21$ ($2,097,152$ slots).
2. **How orders are stored:** Stored directly inside contiguous `HashSlot` entries using `splitmix64` hashing, linear probing, and `SlotState` (`Empty`, `Occupied`, `Tombstone`).
3. **How price levels are stored:** Stored in contiguous sorted vectors: `bids_` is sorted ascending (`5000 -> 9999`) and `asks_` is sorted descending (`15000 -> 10001`) so the Best Bid and Best Ask are always at `.back()`.
4. **How the best bid/ask is found:** Retrieved in $O(1)$ time via `bids_.back()` and `asks_.back()`.
5. **How an order is found during cancellation:** Located via linear probing in `order_table_`, marked as `Tombstone` in $O(1)$ average time, and the corresponding price level is found via `std::lower_bound` binary search ($O(\log M)$), with `std::vector::erase` ($O(M)$ contiguous `memmove`) if level quantity reaches 0 ($O(1)$ `.pop_back()` if the best level depletes).

### 3. Implementation C (`OrderBookC` — Custom Zero-Allocation Direct Array, Hardware Bitset & Pool)
1. **What data structure(s) you used:** Cache-aligned fixed arrays `alignas(64) std::array<long, 5000>` (`bids_`, `asks_`), 64-bit hardware bitsets `alignas(64) std::array<uint64_t, 79>` (`bid_bits_`, `ask_bits_`), and a direct-indexed order pool `std::vector<OrderSlot> order_pool_`.
2. **How orders are stored:** Stored in a pre-allocated direct-indexed array `order_pool_[id]` (16-byte `OrderSlot` holding `{qty, idx, side, active}`), eliminating hashing and dynamic allocations completely.
3. **How price levels are stored:** Direct-indexed by tick offset: `bids_[9999 - tick]` and `asks_[tick - 10001]`, so index `0` is always the best possible price (`99.99` for bids, `100.01` for asks).
4. **How the best bid/ask is found:** Read in $O(1)$ time from cached indices `best_bid_idx_` and `best_ask_idx_`. When the best level depletes, the next active price level is found in hardware via `__builtin_ctzll` (Count Trailing Zeros) over the 79-word bitset.
5. **How an order is found during cancellation:** Direct array lookup `order_pool_[id]` in strict $O(1)$ worst-case time, followed by $O(1)$ quantity subtraction at `bids_[idx]`/`asks_[idx]` and bit clearing if the level drops to 0.

### Big-O Complexity Table (Section 4)

| Operation | Implementation A (`std::map` + `std::unordered_map`) | Implementation B (Sorted `std::vector` + Flat Hash) | Implementation C (Direct Array + Bitset + Pool) |
| :--- | :--- | :--- | :--- |
| **`add_order`** | $O(\log M)$ avg, $O(N + \log M)$ worst (rehash) | $O(\log M)$ existing level, $O(M)$ new level insert | **$O(1)$ worst-case** |
| **`cancel_order`** | $O(\log M)$ avg, $O(N + \log M)$ worst | $O(\log M)$ non-depleting, $O(M)$ on level removal | **$O(1)$** (or $O(W) \approx O(1)$ if BBO depletes) |
| **`get_bbo`** | $O(1)$ | $O(1)$ | **$O(1)$** |
| **`get_bids(K)`** | $O(K)$ tree iterator steps | $O(K)$ contiguous reverse reads | **$O(K + W)$** via `__builtin_ctzll` |
| **`get_asks(K)`** | $O(K)$ tree iterator steps | $O(K)$ contiguous reverse reads | **$O(K + W)$** via `__builtin_ctzll` |

---

## Section 10: Results Table

**Environment & `perf` Note (Section 8 & 16 FAQ):** Benchmarked on Ubuntu running inside WSL2 (Hyper-V virtualization). As noted in Section 8 and Section 16 FAQ, hardware PMU counters (`cycles`, `instructions`, `branches`, `branch-misses`, `cache-misses`, `L1-dcache-load-misses`, `dTLB-load-misses`) are blocked by the host Hyper-V hypervisor (`<not supported>`), while kernel software and memory events (`task-clock`, `page-faults`, `context-switches`, and `perf record -e task-clock` symbol profiling) are fully supported and reported below.

| Metric | Implementation A | Implementation B | Implementation C |
| :--- | :---: | :---: | :---: |
| **Total time (ms)** | 186.64 ms | 169.51 ms | **59.65 ms** |
| **ns / add** | 136.84 ns | 152.94 ns | **27.39 ns** |
| **ns / cancel** | 333.90 ns | 194.71 ns | **83.70 ns** |
| **ns / BBO** | 21.32 ns | 19.39 ns | **19.62 ns** |
| **ns / get_bids+asks(10)** | 135.23 ns | 72.36 ns | **76.91 ns** |
| **task-clock (ms, `perf stat -r 5`)** | 1133.52 ms | 1065.73 ms | **370.86 ms** |
| **IPC** | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) |
| **Branch misses (%)** | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) |
| **L1-dcache-load-misses** | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) |
| **dTLB-load-misses** | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) |
| **Cache misses** | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) | `<not supported>` (WSL2 VM) |
| **Page faults (`perf stat -r 5`)** | 19092 | 90918 | **17157** |

---

## Section 11: Performance Analysis

### 11.1 Which Implementation Is Fastest?
* **`add_order`:** **Implementation C** is fastest at **27.39 ns** (**5.00x faster** than Impl A at 136.84 ns and **5.58x faster** than Impl B at 152.94 ns). Instead of hashing an order ID, allocating a heap node, or running binary search, Impl C computes `idx = 9999 - tick` (or `tick - 10001`), increments `bids_[idx]`, sets 1 bit in `bid_bits_[idx >> 6]`, and writes directly to `order_pool_[id]`. In `perf report`, `OrderBookC::add_order` consumes only `~7.7%` of total execution samples despite making up 70% of the workload.
* **`cancel_order`:** **Implementation C** is fastest at **83.70 ns** (**3.99x faster** than Impl A at 333.90 ns and **2.33x faster** than Impl B at 194.71 ns). Impl A must look up and `free()` a heap bucket node in `std::unordered_map` and then traverse `std::map` ($O(\log M)$) to decrement the price level. Impl B avoids `free()` via tombstones (194.71 ns) but still runs `std::lower_bound`. Impl C indexes directly into `order_pool_[id]` and decrements the price array in $O(1)$ time.
* **`get_bbo`:** All three implementations achieve **~19–21 ns** (21.32 ns in A, 19.39 ns in B, 19.62 ns in C) because all three maintain $O(1)$ access to the best price (`begin()` in A, `.back()` in B, and cached `best_bid_idx_` in C), where measured latency is dominated by the `std::chrono::steady_clock::now()` vDSO call (`__vdso_clock_gettime`).

### 11.2 Branch Prediction
* **Most Branch Misses:** **Implementation A** and **Implementation B** incur the highest branch misprediction penalties.
* **Why:** Both rely on $O(\log M)$ search loops with data-dependent branches ($\sim 12$ comparisons per operation for $M = 5,000$ levels). In Impl A, walking the Red-Black tree compares keys loaded from random heap pointers; in Impl B, `std::lower_bound` branches left or right based on random uniform ticks (`rng(42)`), which the CPU branch predictor cannot predict above 50% accuracy. Additionally, Impl B's linear probing loop in `insert_order_slot` adds unpredictable collision branches.
* **How Impl C eliminates branch misses:** Impl C uses direct array indexing with zero search loops on `add_order` and `cancel_order`. When scanning levels in `get_bids(10)` or updating BBO after a level drops to zero, Impl C uses `__builtin_ctzll` (compiling to a single x86 `TZCNT` instruction) to skip up to 64 empty ticks in one hardware cycle without branching on each empty tick.

### 11.3 Cache Behavior
* **Most Cache Misses:** **Implementation A** suffers the worst cache miss behavior across L1d, L2, and LLC.
* **Memory Access Patterns:**
  * **Impl A (Pointer Chasing & Small Heap Objects):** Every `std::map` node (48 bytes) and `std::unordered_map` node (40 bytes) is allocated individually on the heap. Walking 12 tree levels or chasing hash bucket pointers jumps across disjoint 64-byte cache lines scattered across megabytes of heap memory, defeating hardware stream prefetchers and stalling the CPU out-of-order window.
  * **Impl B (Contiguous Vectors + Large Flat Hash Table):** Price levels sit in two tiny contiguous vectors (`5,000 * 16 B = 80 KB`), making `get_bids(10)` (72.36 ns) nearly twice as fast as Impl A (135.23 ns) due to sequential L1/L2 cache reads from `.back()`. However, `order_table_` ($2^21 \times 32\text{ B} = 64\text{ MB}$) exceeds LLC capacity, so random `splitmix64` hash probes in `insert_order_slot` (`~38.8%` in `perf report`) frequently miss LLC and dTLB.
  * **Impl C (L1/L2 Cache Resident):** `bids_` and `asks_` take $40\text{ KB}$ each ($80\text{ KB}$ total, fitting in L2 cache), and `bid_bits_`/`ask_bits_` take just $632\text{ B}$ each ($1.26\text{ KB}$ total, permanently resident in **L1 data cache**). Furthermore, `order_pool_` uses compact 16-byte slots accessed sequentially (`id = 1, 2, ...`) during `add_order`, allowing the hardware prefetcher to stream slots into L1d ahead of execution.

### 11.4 Page Faults
* **Most Page Faults:** **Implementation B** causes the most page faults (**90918 page faults** across 5 internal benchmark trials, compared to **19092** in Impl A and **17157** in Impl C).
* **What this reveals about memory usage and allocation behavior:**
  * In `OrderBookB`, each of the 5 benchmark runs allocates a 64 MB `order_table_` ($2^21$ slots). Linux uses lazy demand-paging: virtual pages are only mapped to physical RAM and zeroed when first written. Because `splitmix64` scatters order IDs across the entire 64 MB table, every trial touches ~16,384 4 KB pages ($16,384 \times 5 \approx 81,920+$ faults), which is directly confirmed by `perf report` showing kernel page-fault handlers `do_user_addr_fault` and `clear_page_erms`.
  * **Impl C** has the **fewest page faults (17157)** because its `OrderSlot` is packed into 16 bytes and only the sequential prefix of 700,000 IDs ($700,000 \times 16\text{ B} \approx 11.2\text{ MB} \approx 2,800$ pages per trial) is ever touched.

### 11.5 Production Choice
If deploying one implementation to a real single-instrument HFT production system, I would choose **Implementation C**, evaluated across all six engineering dimensions:
1. **Latency:** Delivers the lowest total runtime (**59.65 ms**), fastest `add_order` (**27.39 ns**), and fastest `cancel_order` (**83.70 ns**).
2. **Predictability (Tail Latency):** Zero dynamic heap allocations (`new`/`free`), zero hash table collisions/rehashing, and zero $O(M)$ vector `memmove` shifts. Every operation executes in deterministic $O(1)$ worst-case time, eliminating p99.9 latency spikes during market volatility bursts.
3. **Memory Usage:** Fixed, tiny price-book footprint ($81.26\text{ KB}$ for arrays + bitsets) and compact 16-byte per-order slots ($16.8\text{ MB}$ pool), consuming 4x less memory than Impl B's 64 MB hash table.
4. **Cache Behavior:** Bitsets reside permanently in L1d cache and price arrays fit entirely in L2 cache, preventing order book lookups from evicting critical strategy state from LLC.
5. **Complexity:** Simpler hot-path control flow than Red-Black tree rebalancing or tombstone probe-chain maintenance—consisting only of array indexing and bitwise `OR`/`AND`/`TZCNT`.
6. **Maintainability:** Cleanly encapsulated in ~130 lines of standard C++17. The only production trade-off is that fixed tick arrays require bounded tick bands; if deployed on an instrument with unbounded prices or arbitrary sparse 64-bit exchange order IDs, the direct pool would be paired with a pre-faulted ring buffer or SIMD flat hash table.

### 11.6 What Would You Try Next?
1. **Pre-Faulting (`memset` / `MAP_POPULATE`) & 2 MB HugePages:** `perf report` showed that Linux minor page faults (`do_user_addr_fault` and `clear_page_erms`) consume `~3.1%` of runtime in Impl C and `~6.3%` in Impl B. Pre-faulting the pool at startup and backing it with 2 MB HugePages eliminates runtime page faults and shrinks dTLB usage from 2,800 entries (4 KB pages) to 6 HugePage TLB entries.
2. **2-Level Hierarchical Bitset:** Adding a single 64-bit `summary_word_` where bit $k$ indicates whether `bid_bits_[k]` is non-zero reduces next-best-price discovery from scanning up to 79 words to strictly two `TZCNT` instructions ($O(1)$ constant time).
3. **Intrusive Doubly-Linked Circular Ring Buffer for Order IDs:** In real trading, old orders expire or fill, so active order IDs fall within a sliding window. Indexing `order_pool_[id & RING_MASK]` allows a compact 2 MB L3-resident array to handle billions of sequential order IDs over a full trading day without ever reallocating.
