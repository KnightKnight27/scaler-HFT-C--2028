# Data structures, invariants, and complexity

All three designs store each order's side, tick price and quantity so cancellation
can remove exactly that contribution. Only positive-quantity levels are active.
No operation matches orders. All code is single-threaded.

## A: standard hash lookup and ordered price maps

`orders_` is `std::unordered_map<uint64_t, Order>`. Each side is a
`std::map<int, long>` whose value is aggregate quantity. Add inserts the order
and increases its level. Cancel finds the order, subtracts its quantity, erases
an empty level and removes the ID. Map endpoints expose the best prices; forward
asks and reverse bids provide depth in price order. The maps handle arbitrary
valid prices without a fixed price grid in their storage.

## B: tree order lookup and sorted level vectors

`orders_` is `std::map<uint64_t, Order>`, giving deterministic logarithmic ID
lookup. Each side stores `Level` objects in an ascending vector. `lower_bound`
locates the price; inserting a new level or removing an empty level shifts
subsequent elements. Existing-level updates need no shift. Endpoint BBO access
is constant time and depth traversal is contiguous. This independently changes
both per-order and per-price structures compared with A.

## C: flat order table and fixed price grid

Orders occupy a power-of-two `vector<Slot>`. A 64-bit integer mixer spreads all
ID bits, and linear probing resolves collisions. Slot state distinguishes empty,
live and deleted entries: an unknown lookup stops at an empty slot but crosses
tombstones. Erasure leaves a tombstone, preserving collided probe chains. Before
used occupancy reaches 70%, rebuilding removes tombstones; the table doubles
when live occupancy is too high for a same-capacity rebuild. Order IDs need not
be dense, monotonic, or small. This is not a direct ID-indexed vector.

For each side, 5,000 `long` quantities map directly from ticks. Buy index is
`price - 5000`; sell index is `price - 10001`. Each side also has 79 occupancy
words and a cached best index. Adding a new level sets its bit. Removing a
level clears it; if it was best, the next occupied bit becomes best. Portable
C++17 bit searches use bounded shifts, including at bit 63 and in the partial
last word. Depth walks occupied bits in best-to-worse order, skipping empty words.

The table never shrinks. Its capacity reflects peak usage, and rehashing holds
both old and new storage temporarily. Arrays give bounded price storage, but
order insertion still has allocation and rebuild latency spikes.

## Complexity

N = live orders; L = active levels on the accessed side; K = min(requested positive
depth, L); P = 5,000 prices per side; W = ceil(P/64) = 79; M = C's hash capacity,
which can exceed current N after cancellations. Zero/negative depth is O(1).

| Operation | A | B | C |
| --- | --- | --- | --- |
| `add_order` | Expected/amortized O(1 + log L); worst O(N + log L) | O(log N + L) worst; existing level O(log N + log L) | Expected/amortized O(1); a normal insert can probe O(M); a pathological rebuild can reach O(M²) |
| `cancel_order` | Expected O(1 + log L); worst O(N + log L) | O(log N + L) worst; nonempty level O(log N + log L) | Expected O(1), plus up to O(W) to replace an emptied best; worst O(M + W) |
| `get_bbo` | O(1) on this standard-library map implementation | O(1) | O(1) |
| `get_bids` | O(K) | O(K) | O(K + W) worst; often O(K) in a dense book |
| `get_asks` | O(K) | O(K) | O(K + W) worst; often O(K) in a dense book |
| Storage | O(N + L_bid + L_ask) | O(N + vector capacities) | O(M + P) |

Hash-table expectations assume non-adversarial IDs. C's deterministic hash is
not a defense against deliberately engineered collisions. Its rebuild scans
capacity M and reinserts live entries: expected O(M), potentially quadratic
under extreme clustering. B has no expected-case hash assumption, but has
vector shifts and dynamic allocation. Depth results allocate their own vectors
in every implementation; their storage is O(K).

These asymptotic costs do not capture allocation, cache misses, branch prediction
or frequency changes. The actual workload quickly fills nearly all prices, so
B mostly updates existing levels; it does not repeatedly shift all 5,000 entries
on every add. C pays for flat-table growth during this workload. Neither fact
should be hidden when interpreting benchmark results.
