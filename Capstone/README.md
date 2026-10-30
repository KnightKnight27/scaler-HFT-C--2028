Student details
email: rama.24bcs10087@sst.scaler.com
roll_no: 24BCS10087

# Capstone: Limit Order Book (price-time priority)

A single-instrument limit order book with a matching engine, written in C++23.

```cpp
class OrderBook {
  std::expected<std::span<const Trade>, BookError> addOrder(Order order);
  bool removeOrder(OrderId id);
  std::vector<LevelSnapshot> getBids(std::size_t depth = kFullDepth) const;
  std::vector<LevelSnapshot> getAsks(std::size_t depth = kFullDepth) const;
  BBO getBBO() const;
};
```

- `addOrder` matches the incoming order against the opposite side and returns the trades it generated.
  Anything left over rests in the book at its limit price.
  It rejects duplicate ids, zero quantity and non-positive prices.
- Trades execute at the **maker's** price. Better prices fill first, and orders at the same price fill in arrival (FIFO) order.
- `removeOrder` cancels a resting order in O(1) once it has been found by id.
- Prices are integer ticks (1 tick = 0.01 rupee, see `util/price.h`), so price comparisons never hit floating-point error.

## Design

| Component | Choice | Why |
|---|---|---|
| `OrderPool` | `std::vector<OrderNode>` with an intrusive free list | No heap allocation per order once warm. Nodes are linked by 32-bit indices, which stay valid when the vector grows |
| `PriceLevel` | Intrusive doubly linked FIFO (head/tail indices) + aggregated qty/count | O(1) append, O(1) cancel from the middle, O(1) BBO quantity |
| `BookSide<Side>` | Sorted flat `std::vector<PriceLevel>`, **best price at the back** | Matching and new orders mostly touch the top of the book, so `back()` / `pop_back()` are O(1). A contiguous array of 1000 levels is far more cache friendly than `std::map` nodes |
| Id lookup | `std::unordered_map<OrderId, NodeIndex>` (pre-reserved) | O(1) cancel |
| Trade output | Internal buffer that is reused, returned as `std::span` | No allocation per `addOrder`. The span stays valid until the next `addOrder` |

Compared with the options in `capstone.txt`:
- **Priority queue + lazy deletion:** cancelled orders stay in memory until they reach the top of the heap.
- **`std::multiset`:** every order is its own heap node, which means poor locality.
- **`std::map<Price, std::vector<Order>>`:** cancelling from the middle of a level is O(n).

The flat-vector approach here avoids all three problems.

## Build & run

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/order_book_tests   # or: ctest --test-dir build
./build/order_book_demo    # Kartik / Manik / Paras scenario from the notes
./build/order_book_bench
```

Release builds use `-O3 -march=native`.

## Benchmark

5M pre-generated operations: 65% limit orders spread over 500 levels per side, some of which cross the spread, and 35% cancels of recent ids.

```
throughput: 16.72 M ops/s (59.8 ns/op)
latency ns (incl. clock overhead): p50 70 | p90 80 | p99 110 | p99.9 1483
```

## Future improvements

- Replace `std::unordered_map` with an open-addressing flat hash map. It is the hottest structure on the cancel path.
- Index levels directly by `price - base_price` for a bounded price band. Every level lookup would then be O(1) instead of a binary search.
- Add market, IOC and FOK order types, and order modify (reduce quantity in place without losing priority).
