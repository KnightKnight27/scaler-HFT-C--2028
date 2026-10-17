# Capstone: Build a Low-Latency Order Book (C++17)

**Team Members:**
1. **Name:** Utkarsh Raj | **SST Email:** utkarsh.24bcs10318@sst.scaler.com | **Roll Number:** 24BCS10318
2. **Name:** Ojas Sinha | **SST Email:** ojas.24bcs10077@sst.scaler.com | **Roll Number:** 24BCS10077

---

## 1. How to Build the Project

The project uses standard C++17 (`std` library only, zero external dependencies) and compiles cleanly with the exact flags required in Section 2 of the project specification:

    g++ -O3 -g -std=c++17 -march=native -Wall -Wextra -Iinclude \
        src/orderbook_a.cpp src/orderbook_b.cpp src/orderbook_c.cpp \
        src/workload.cpp src/main.cpp -o bench

Alternatively, use the included `Makefile`:

    make

---

## 2. How to Run the Correctness Test & Benchmarks

### Run Correctness Verification (Section 5)

    ./bench --verify
    # Or via Makefile (saves output to result/correctness.txt):
    make verify

### Run Each Implementation Benchmark (Section 6 & 7)

    ./bench --impl A
    ./bench --impl B
    ./bench --impl C
    # Or via Makefile (saves all 3 outputs to result/benchmark.txt):
    make run-bench

### Run Linux `perf stat` and `perf record` Profiling (Section 8)

    make perf
    # Or run the entire verification, benchmark, and profiling suite at once:
    make results

---

## 3. Interface Changes & Price Representation Notes (Section 3 & Hint 3)

The public API of `OrderBookA`, `OrderBookB`, and `OrderBookC` matches Section 3 verbatim:

    enum class Side { Buy, Sell };
    struct Level { double price; long qty; };
    struct BBO { bool has_bid; double bid_price; long bid_qty; bool has_ask; double ask_price; long ask_qty; };

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

**Internal Integer Tick Representation:**  
While keeping the exact public `double price` signature unchanged, all three implementations immediately convert `double price` at the API boundary into an exact 32-bit integer tick (`include/types.hpp`):

    int32_t tick = static_cast<int32_t>(std::llround(price * 100.0));

* **Bid range `[50.00, 99.99]`:** Mapped to integer ticks `[5000, 9999]` (5,000 discrete price levels).
* **Ask range `[100.01, 150.00]`:** Mapped to integer ticks `[10001, 15000]` (5,000 discrete price levels).
* Integer ticks are converted back to `double` (`tick * 0.01`) only when constructing `BBO` and `Level` return values. This completely prevents IEEE-754 floating-point precision drift.

---

## 4. Data-Structure Choices Overview (Section 4)

| Design Axis | Implementation A (`OrderBookA`) | Implementation B (`OrderBookB`) | Implementation C (`OrderBookC`) |
| :--- | :--- | :--- | :--- |
| **1. Underlying Data Structures** | `std::map` (Red-Black Tree) + `std::unordered_map` (Node Hash Table) | Sorted `std::vector<TickLevel>` + Custom Flat Open-Addressing Hash Table (`std::vector<HashSlot>`) | Fixed `alignas(64) std::array<long, 5000>` + 64-bit Hardware Bitset (`std::array<uint64_t, 79>`) + Direct Pool |
| **2. How Orders Are Stored** | Heap-allocated hash bucket nodes in `std::unordered_map<uint64_t, OrderInfo>` | Contiguous $2^{21}$-slot open-addressing array with `splitmix64` hashing and `Tombstone` state markers | Pre-allocated direct-indexed vector `order_pool_[id]` (16 B `OrderSlot`, zero hashing) |
| **3. How Price Levels Are Stored** | Heap-allocated Red-Black Tree nodes (`bids_` sorted descending, `asks_` sorted ascending) | Contiguous sorted vectors (`bids_` ascending `5000..9999`, `asks_` descending `15000..10001` so best prices sit at `.back()`) | Direct-indexed arrays: `bids_[9999 - tick]` and `asks_[tick - 10001]` |
| **4. How Best Bid/Ask (BBO) Is Found** | Dereferencing `bids_.begin()` and `asks_.begin()` ($O(1)$) | Reading `bids_.back()` and `asks_.back()` ($O(1)$) | Reading cached `best_bid_idx_` / `best_ask_idx_`; updated via hardware `__builtin_ctzll` on bitset when level depletes |
| **5. How Orders Are Found on Cancel** | Hash lookup in `orders_` ($O(1)$ avg) + tree search & rebalance in `bids_`/`asks_` ($O(\log M)$) | Linear probe in `order_table_` ($O(1)$ avg) + `std::lower_bound` binary search ($O(\log M)$) and `memmove` on level removal | Direct array index `order_pool_[id]` ($O(1)$ worst-case) + $O(1)$ array subtraction & bit clear |
