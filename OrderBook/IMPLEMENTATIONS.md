# Implementation designs

All three derive from `OrderBook` in `order.h` and use the same five methods.
Prices arrive as `double` and are stored as integer ticks internally
(`99.50 -> 9950`), so no comparisons happen on floating point.

Throughout, `N` = number of live orders, `P` = number of active price levels
(`P <= 10000`), `K` = requested depth.

---

## A — `std::map` + `unordered_map`

The straightforward ordered-tree version.

- **Orders**: `std::unordered_map<uint64_t, Info>` where `Info = {Side, tick, qty}`.
- **Price levels**: two `std::map<long, long>` — bids sorted with
  `std::greater<long>` (highest first) and asks with `std::less<long>`
  (lowest first). The value is the total qty resting at that level.
- **Best price**: the first element of each map, so `begin()`. A red-black
  tree keeps this sorted for free.
- **Cancel**: find the id in the hash map, then `find()` the tick in the right
  map, subtract the qty, and `erase()` the node if the level hits zero.

### Complexity

| Operation | Average | Worst |
| :--- | :--- | :--- |
| `add_order` | O(log P) | O(log P) |
| `cancel_order` | O(log P) | O(log P) |
| `get_bbo` | O(1) | O(1) |
| `get_bids(K)` | O(K) | O(K) |
| `get_asks(K)` | O(K) | O(K) |

`add`/`cancel` pay a tree walk every time, but `get_bbo` is a single pointer
read, which is why A wins the BBO column.

---

## B — two priority queues + lazy deletion

Same idea as the hints: keep the best price at the top of a heap.

- **Orders**: `std::unordered_map<uint64_t, std::pair<PLevel*, long>>` —
  a pointer to the price level plus the qty this order put in. So an order
  does not own its own node, it just points into the shared level.
- **Price levels**: heap-allocated `PLevel { tick, qty }` objects. `PLevel`s
  live in two `std::priority_queue`s (max-heap for bids, min-heap for asks)
  and are also indexed by tick in an `unordered_map` for O(1) lookup.
- **Best price**: the top of the heap.
- **Cancel**: lazy. Just subtract the order's qty from the level it points to
  and forget the id. The level stays in the heap even if its qty is now 0. The
  dead levels get thrown away later when they bubble to the top during a query
  (`drop_zeros()`).

The lazy part is the whole point: cancelling never touches the heap. The cost
is that a level which went to zero earlier can sit in the heap until a query
bumps into it.

### Complexity

| Operation | Average | Worst |
| :--- | :--- | :--- |
| `add_order` | O(1) | O(log P) |
| `cancel_order` | O(1) | O(1) |
| `get_bbo` | O(1) amortized | O(P log P) |
| `get_bids(K)` | O(K log P) | O(P log P) |
| `get_asks(K)` | O(K log P) | O(P log P) |

`add` is O(1) when it lands on an existing level (hash hit) and O(log P) only
for a new level. `get_bids`/`get_asks` pop K entries and push the live ones
back, hence `2K log P`.

---

## C — fixed array indexed by tick (my own design)

The price universe is small and known up front, so a search structure is not
really needed.

- **Orders**: `std::unordered_map<uint64_t, Info>` — same idea as A, needed
  only so cancel can find what to subtract.
- **Price levels**: two `std::vector<long>` of size 5000. Bids are indexed by
  `tick - 5000` (tick 5000..9999) and asks by `tick - 10001`
  (tick 10001..15000). The value is the qty at that tick. No nodes, no
  pointers, all contiguous.
- **Best price**: scan. For bids walk down from index 4999 until a nonzero
  slot, for asks walk up from index 0. Because prices are spread over the
  whole band the first populated slot is usually found almost immediately.
- **Cancel**: hash lookup, subtract from the array slot.

The downside is the scan: if one side of the book is empty (or the top of the
band is empty) `get_bbo` can walk a long way, and depth queries walk over
empty slots too.

### Complexity

| Operation | Average | Worst |
| :--- | :--- | :--- |
| `add_order` | O(1) | O(1) |
| `cancel_order` | O(1) | O(1) |
| `get_bbo` | O(R) | O(10000) |
| `get_bids(K)` | O(R + K) | O(10000) |
| `get_asks(K)` | O(R + K) | O(10000) |

`R` is the distance from the edge of the band to the first non-empty level.
Both add and cancel are a single array bump regardless of how big the book is.

---

## Side by side

| | A | B | C |
| :--- | :--- | :--- | :--- |
| add | tree walk, O(log P) | hash + heap push, ~O(1) | array bump, O(1) |
| cancel | tree erase, O(log P) | lazy subtract, O(1) | array bump, O(1) |
| bbo | O(1) begin() | O(1) amortized top() | scan, O(R) |
| memory | N + P tree nodes | N + P levels + tombstones | N + 10000 longs |
| weak spot | every op walks a tree | stale levels pile up | scanning / empty side |

No implementation is strictly best everywhere, which is the interesting part.
