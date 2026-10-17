# Order Book Architecture & Implementation Designs

This document provides a detailed technical breakdown of the three order book implementations in accordance with the capstone specifications. All three implementations implement the common `IOrderBook` interface defined in [`order.h`](order.h), support prices as floating-point multiples of `0.01` internally converted to integer ticks ($50.00 \to 5000, 150.00 \to 15000$), and adhere strictly to resting order book semantics with zero order matching.

---

## 1. Summary of Architectural Approaches

| Dimension | Implementation A (`std::map`) | Implementation B (Priority Queue + Lazy Delete) | Implementation C (Sorted `std::list`) |
| :--- | :--- | :--- | :--- |
| **Order Storage** | `std::unordered_map<uint64_t, OrderInfo>` | `std::unordered_map<uint64_t, std::pair<Order*, long>>` | `std::unordered_map<uint64_t, Order*>` |
| **Price-Level Storage** | Two `std::map<long, long>` (Red-Black Trees) | Aggregated `Order*` nodes + two `std::priority_queue` | Implicit: consecutive `Order*` nodes in `std::list` |
| **Best-Price Lookup** | O(1) via `m_bids.begin()` / `m_asks.begin()` | Amortized O(1) via `m_bids.top()` (lazy prune) | O($M_{best}$) scan along list head to sum qty |
| **Cancel Lookup** | O(1) hash map lookup + O(log P) tree erase | O(1) hash map lookup + O(1) in-place subtraction | O(1) hash map lookup + O(1) list iterator erase |
| **Primary Bottleneck** | Pointer chasing & branch misses during tree traversal | Hash table bucket collisions & rehashing | O(N) linked list traversal during insertion |

---

## 2. Implementation A — Red-Black Tree (`std::map`) & Hash Map

### 2.1 Design Overview
Implementation A utilizes standard ordered associative containers (`std::map`) to maintain sorted price levels, coupled with a hash map (`std::unordered_map`) for fast order lookup by ID.

### 2.2 Data Structures
- **Order Storage (`m_orders`)**:  
  `std::unordered_map<uint64_t, OrderInfo>`  
  Maps each unique order ID to an `OrderInfo` struct:
  ```cpp
  struct OrderInfo {
      Side side;
      long price; // integer tick: price * 100
      long qty;
  };
  ```
- **Price-Level Storage (`m_bids`, `m_asks`)**:  
  - Bids: `std::map<long, long, std::greater<long>>` (ordered descending; highest bid at `begin()`).  
  - Asks: `std::map<long, long, std::less<long>>` (ordered ascending; lowest ask at `begin()`).  
  Each tree node stores a tick price key and the cumulative aggregate quantity resting at that price level.

### 2.3 Operation Breakdown
- **`add_order(id, side, price, qty)`**:
  1. Computes tick price via `price_to_ticks(price)`.
  2. Stores order information in `m_orders[id]`.
  3. Inserts or updates the price level quantity in `m_bids[tick_price] += qty` or `m_asks[tick_price] += qty`.
- **`cancel_order(id)`**:
  1. Looks up `id` in `m_orders`. Returns `false` if not found.
  2. Locates the price level in `m_bids` or `m_asks` using `info.price` via `find()`.
  3. Subtracts `info.qty`. If the aggregate quantity reaches 0, the node is erased from the red-black tree (`erase(map_it)`).
  4. Erases `id` from `m_orders` and returns `true`.
- **`get_bbo()`**:
  1. Inspects `m_bids.begin()` for the highest bid and `m_asks.begin()` for the lowest ask.
  2. Returns a populated `BBO` struct in $O(1)$ time without searching.
- **`get_bids(depth)` / `get_asks(depth)`**:
  Iterates sequentially through the first $\min(\text{depth}, \text{size})$ tree nodes starting from `begin()`.

### 2.4 Complexity Analysis

| Operation | Average Complexity | Worst-Case Complexity | Rationale |
| :--- | :--- | :--- | :--- |
| `add_order` | **$O(\log P)$** | $O(N + P)$ | $O(1)$ average hash insert + $O(\log P)$ tree insertion/rebalancing ($P \le 10,000$). |
| `cancel_order` | **$O(\log P)$** | $O(N + \log P)$ | $O(1)$ average hash lookup + $O(\log P)$ tree search & node deletion. |
| `get_bbo` | **$O(1)$** | **$O(1)$** | Pointer dereference of `m_bids.begin()` and `m_asks.begin()`. |
| `get_bids(K)` | **$O(K)$** | **$O(K)$** | In-order tree iterator traversal across at most $K$ adjacent levels ($K \ll P$). |
| `get_asks(K)` | **$O(K)$** | **$O(K)$** | In-order tree iterator traversal across at most $K$ adjacent levels. |

---

## 3. Implementation B — Dual Priority Queues with Lazy Deletion & Hash Maps

### 3.1 Design Overview
Implementation B separates order indexing from priority ordering. It maintains max/min binary heaps (`std::priority_queue`) over price-level pointers and applies **lazy deletion** for cancellations to eliminate expensive heap searches and tree rebalancing on the critical cancel path.

### 3.2 Data Structures
- **Order Storage (`m_orders`)**:  
  `std::unordered_map<uint64_t, std::pair<Order*, long>>`  
  Maps order ID to a pointer to the aggregate price level (`Order*`) and the order's original quantity.
- **Price-Level Storage**:
  - Dynamically allocated `Order` structs representing price levels:
    ```cpp
    struct Order {
        long price; // integer tick
        long qty;   // aggregate active quantity at this tick
        Side side;
    };
    ```
  - `m_priceOrderMap`: `std::unordered_map<long, Order*>` for $O(1)$ lookup from tick price to the level struct.
  - Bids Heap: `std::priority_queue<Order*, std::vector<Order*>, BidCompare>` (max-heap).
  - Asks Heap: `std::priority_queue<Order*, std::vector<Order*>, AskCompare>` (min-heap).

### 3.3 Operation Breakdown
- **`add_order(id, side, price, qty)`**:
  1. Checks `m_priceOrderMap` for `tick_price`.
  2. If the price level already exists, increments `order_ptr->qty += qty`.
  3. If it is a new price level, allocates `new Order{tick_price, qty, side}`, registers it in `m_priceOrderMap`, and pushes it to the appropriate priority queue.
  4. Records `m_orders[id] = {order_ptr, qty}`.
- **`cancel_order(id)` — Lazy Deletion**:
  1. Finds `id` in `m_orders`. Returns `false` if not found.
  2. Directly decrements `order_ptr->qty -= orig_qty` in $O(1)$ via the stored pointer.
  3. Erases the entry from `m_orders`.
  4. **The heap is NOT modified immediately.** If `order_ptr->qty` drops to 0, it remains in the heap until it bubbles to the top and is pruned lazily during subsequent queries.
- **`get_bbo()`**:
  1. Inspects `m_bids.top()`. If `top()->qty == 0`, pops it, erases it from `m_priceOrderMap`, `delete`s the struct, and repeats.
  2. Repeats for `m_asks.top()`.
  3. If active levels remain, returns their values.
- **`get_bids(depth)` / `get_asks(depth)`**:
  Pops elements one-by-one from the priority queue. Stale elements (`qty == 0`) are deleted; active elements are collected and pushed back into the heap upon completion.

### 3.4 Complexity Analysis

| Operation | Average Complexity | Worst-Case Complexity | Rationale |
| :--- | :--- | :--- | :--- |
| `add_order` | **$O(1)$ amortized** | $O(\log P)$ | Existing level: $O(1)$ hash map lookup & update. New level: $O(\log P)$ heap push. |
| `cancel_order` | **$O(1)$** | $O(N)$ | $O(1)$ average hash map lookup + $O(1)$ pointer subtraction. No heap heapify. |
| `get_bbo` | **$O(1)$ amortized** | $O(P \log P)$ | Best case: top is valid ($O(1)$). Stale levels pruned lazily at most once per creation. |
| `get_bids(K)` | **$O(K \log P)$** | $O(P \log P)$ | Pops and re-pushes $K$ active levels ($2K \log P$ heap operations). |
| `get_asks(K)` | **$O(K \log P)$** | $O(P \log P)$ | Pops and re-pushes $K$ active levels ($2K \log P$ heap operations). |

---

## 4. Implementation C — Sorted Doubly-Linked Lists (`std::list`) with Direct Iterators

### 4.1 Design Overview
Implementation C represents an alternative pointer-based linked architecture. Instead of maintaining pre-aggregated price levels, individual orders are placed directly into sorted doubly-linked lists (`std::list`). Each order retains its list iterator to achieve true $O(1)$ structural removal on cancellation.

### 4.2 Data Structures
- **Order Storage (`m_orders`)**:  
  `std::unordered_map<uint64_t, Order*>`  
  Maps order ID to the order's heap node:
  ```cpp
  struct Order {
      uint64_t id;
      Side side;
      long price; // integer tick
      long qty;
      std::list<Order*>::iterator list_it;
  };
  ```
- **Price-Level Storage**:
  - Implicit: Orders rest directly in `std::list<Order*> m_bids` (sorted descending) and `std::list<Order*> m_asks` (sorted ascending).
  - Multiple orders at the same price are kept adjacent. Aggregate quantities are calculated dynamically upon query.

### 4.3 Operation Breakdown
- **`add_order(id, side, price, qty)` — The Linear Scan**:
  1. Allocates `Order* node = new Order{id, side, tick_price, qty, {}}`.
  2. Stores `m_orders[id] = node`.
  3. Traverses the linked list from `begin()` until the first element with a worse price is found:
     - Bids: `while (it != m_bids.end() && (*it)->price > tick_price) ++it;`
     - Asks: `while (it != m_asks.end() && (*it)->price < tick_price) ++it;`
  4. Inserts the node into the list and records `node->list_it`.
- **`cancel_order(id)`**:
  1. Finds `id` in `m_orders`. Returns `false` if not found.
  2. Erases the order from the linked list in $O(1)$ time using the saved iterator: `m_bids.erase(node->list_it)`.
  3. Erases `id` from `m_orders`, frees `node`, and returns `true`.
- **`get_bbo()`**:
  1. Inspects the front node of `m_bids` and `m_asks` for the best price.
  2. Scans forward from `front()` to sum the quantities of all consecutive orders sharing that best price.
- **`get_bids(depth)` / `get_asks(depth)`**:
  Traverses the list from `begin()`, aggregating quantities of consecutive matching-price orders into `Level` objects until `depth` levels are accumulated.

### 4.4 Complexity Analysis

| Operation | Average Complexity | Worst-Case Complexity | Rationale |
| :--- | :--- | :--- | :--- |
| `add_order` | **$O(N)$** | **$O(N)$** | Linear scan across linked list of size $N$ to locate sorted insertion position. |
| `cancel_order` | **$O(1)$** | $O(N)$ | $O(1)$ hash map lookup + $O(1)$ doubly-linked list node unlinking via iterator. |
| `get_bbo` | **$O(M_{best})$** | $O(N)$ | Scans all $M_{best}$ orders residing at the best price level ($M_{best} \le N$). |
| `get_bids(K)` | **$O(M_K)$** | $O(N)$ | Traverses all $M_K$ orders spanning the top $K$ price levels. |
| `get_asks(K)` | **$O(M_K)$** | $O(N)$ | Traverses all $M_K$ orders spanning the top $K$ price levels. |

---

## 5. Architectural Comparison & Trade-Off Summary

```
+-----------------------------------------------------------------------------------------+
|                               DESIGN TRADEOFF MATRIX                                    |
+-------------------+---------------------+-----------------------+-----------------------+
| Criterion         | Implementation A    | Implementation B      | Implementation C      |
|                   | (std::map)          | (Priority Queue)      | (Sorted std::list)    |
+-------------------+---------------------+-----------------------+-----------------------+
| Add Order         | O(log P) tree walk  | O(1) hash / O(log P)  | O(N) linked list scan |
| Cancel Order      | O(log P) tree erase | O(1) in-place deduct  | O(1) iterator unlink  |
| Best Bid/Offer    | O(1) begin() ptr    | O(1) amortized top()  | O(M_best) front scan  |
| Depth Query (K)   | O(K) in-order walk  | O(K log P) pop/push   | O(M_K) list scan      |
| Cache Locality    | Poor (tree nodes)   | High for heap array   | Catastrophic (nodes)  |
| Memory per Level  | 1 tree node (32 B)  | 1 heap ptr + 1 struct | Dynamic order nodes   |
| Scaling Limit     | Logarithmic in P    | Logarithmic in P      | Quadratic in N (O(N²))|
+-------------------+---------------------+-----------------------+-----------------------+
```

### Key Architectural Takeaways:
1. **Implementation B wins overall throughput** because in realistic order book workloads, price levels are reused heavily. B turns most adds and cancels into $O(1)$ hash map operations, deferring heap restructuring until queries occur.
2. **Implementation A delivers the most consistent, predictable latency** for queries (`get_bbo` and `get_bids`/`get_asks`) because the red-black tree maintains an always-valid, pre-aggregated in-order view of active levels.
3. **Implementation C demonstrates the peril of non-contiguous linear search** in high-throughput systems: while cancel is theoretically $O(1)$, the $O(N)$ insertion cost causes total execution time across $N$ operations to scale quadratically as $O(N^2)$, causing severe cache thrashing and CPU pipeline stalls.