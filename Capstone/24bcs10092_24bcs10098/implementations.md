# Implementation architecture

Three independent implementations share one contract. The demo exercises all five
APIs; verification, benchmark and selected-only perf tooling are implemented.
See [analysis.md](analysis.md), [benchmark.md](benchmark.md) and
[profiling.md](profiling.md) for observed results and measurement scope.

## Scope and common API

C++17, Release `-O2`, one instrument and one thread. No matching or trades.
Prices are integer ticks: bid 5000..9999 and ask 10001..15000 inclusive,
corresponding to tick size 0.01 and exactly 5,000 legal levels per side.

`OrderId = uint64_t`, `PriceTicks = int32_t`, `Quantity = int64_t`, and
`Depth = int64_t`. Individual orders and aggregate quantities use the same signed
quantity type. Input prices are already ticks; no floating-point conversion occurs.

```cpp
AddResult add_order(const Order& order);
CancelResult cancel_order(OrderId id);
Bbo get_bbo() const;
DepthSnapshot get_bids(Depth depth) const;
DepthSnapshot get_asks(Depth depth) const;
```

`Order` holds ID, side, price and quantity. `PriceLevel` holds price and total
live quantity. Each BBO side is an independent `optional<PriceLevel>`; its
`has_value()` is the presence flag. Depth returns owned vectors of aggregated
levels, bids descending and asks ascending. Depth counts distinct prices, not
orders. Queries do not mutate the book. Old BBO/depth snapshots remain valid and
unchanged after book mutations; modifying a snapshot cannot modify the book.

## Shared A/B/C contract

| Case | Actual A/B/C behavior |
| --- | --- |
| Valid add | Store the full resting order; return `added`. |
| Invalid enum side | Return `invalid_side`. |
| Price outside the inclusive side range | Return `invalid_price`. |
| Quantity <= 0 | Return `invalid_quantity`. |
| ID accepted previously, live or cancelled, on either side | Return `duplicate_id`. |
| Aggregate would exceed `INT64_MAX` | Return `quantity_overflow`. |
| Unknown/already-cancelled cancellation | Return `not_found`; no mutation. |
| Known cancellation | Remove the entire order; return `cancelled`. |
| Last order removed at a price | Remove the occupied level; C retains its zeroed fixed record with the bit clear. |
| Empty BBO side | `nullopt`, independently of the other side. |
| Empty side or depth <= 0 | Empty depth vector. |
| Depth above available levels | All occupied levels, best-first. |
| ID zero or UINT64_MAX | Valid; subject to permanent uniqueness. |
| Rejected add | No logical state change; ID is not consumed. |

Validation precedence is side, price, quantity, accepted-ID history, aggregate
overflow. All business-input failures return an enum and leave logical state
unchanged. The overflow check uses `qty > max - total` before adding, avoiding
signed overflow. There is no book-wide quantity sum, only per-level totals.

IDs cannot be reused for the lifetime of a book instance. `used_ids_` records
successful adds permanently; cancel only removes the corresponding `live_`
entry. Invalid adds and unknown cancellations consume no ID. There is no reset
API. Separate new book instances have independent ID histories.

A stores FIFO order through list insertion order, so no timestamp or arrival
counter is needed. Orders are never sorted by ID. A has no sequence-exhaustion
condition. The current public contract has no caller timestamp; FIFO follows successful
call order.

## A: Ordered maps and FIFO lists

`impl_a/orderbook_a.hpp` declares `OrderBookA`. Its source owns:

- Two ascending `std::map<PriceTicks, Level>` containers, one per side.
- Each occupied level holds a cached total and a `std::list<Order>` in FIFO order.
- `unordered_map<OrderId, Locator> live_` holds side, map iterator and list iterator.
- `unordered_set<OrderId> used_ids_` permanently remembers accepted IDs.

Both maps use the same ascending map type, keeping cancellation locators simple.
Bid queries use reverse iterators, while ask queries use forward iterators.

1. `add_order`: validate, look up the price, check aggregate overflow, create a
   level if needed, append the list node and insert both ID records. Commit the
   total only after all insertions succeed.
2. `cancel_order`: find the live locator, subtract its node quantity, unlink the
   node and erase the live record; erase the map level if its list becomes empty.
3. `get_bbo`: copy the bid reverse endpoint and ask forward endpoint, including
   cached totals; leave absent sides empty.
4. `get_bids`: copy up to depth reverse-map levels into an owned vector.
5. `get_asks`: copy up to depth forward-map levels into an owned vector.

Add rolls back a newly created level, appended node and accepted-ID insertion if
a later allocation fails. `std::bad_alloc` propagates; there is no noexcept promise.
Logical contents are restored, although a hash table may retain increased bucket
capacity. Deterministic allocation-failure injection is not part of the current
test suite; the rollback paths are code-reviewed, not fault-injection verified.
Snapshot allocation can throw without altering book state.

Copy and move are explicitly disabled because locators refer to the instance's
nodes. No custom allocator, pool, threads or external library is used.

## Invariants and verification

- Every live order has legal side/price, positive quantity and one unique ID.
- Every live ID is in accepted-ID history; cancelled IDs remain in that history.
- Each map level is nonempty and its cached total equals its FIFO-node sum.
- Every locator identifies the correct side, map level and order node.
- Live-node count equals live-index size; there are no unindexed or extra nodes.
- FIFO preserves insertion order after first/middle/last cancellations.
- BBO prices are map endpoints and BBO quantities equal level totals.
- Rejected business operations do not change snapshots, totals or ID history.

`verify.cpp` defines 20 deterministic named A test cases with explicit expected
prices, totals and FIFO IDs. Helpers check internal invariants after each wrapped
add/cancel. A friend test-access struct is defined only in verification code; it
does not add operations to the public API. Tests use runtime checks rather than
assert, so Release `-DNDEBUG` does not disable them. They cover both sides, empty
books, endpoints, invalid inputs, all cancellation positions, history, overflow,
owned snapshots and dispatch through the abstract interface. The 20 A cases form a shared templated suite and also run
against B and C. These are not random differential tests.

## B: Indexed binary heaps

`impl_b/orderbook_b.hpp` and `.cpp` define `OrderBookB`, independently of A.
Each side stores individual orders in a contiguous `vector<Entry>`. An entry
contains the shared `Order` and an internal uint64_t arrival sequence. Bids
prioritize higher prices, asks lower prices; price ties prioritize smaller
sequences. IDs do not determine priority. Copy/move are disabled for a simple
instance-lifetime policy consistent with A.

`live_` maps ID to side and vector position. `bid_totals_` and `ask_totals_`
are unordered price-to-quantity maps, never ordered maps. `used_ids_` retains
all accepted IDs permanently, independently of whether their orders are live.
The production B implementation does not depend on A or use std::map.

### Five operations and cancellation

- Add uses the same validation precedence and enum results as A, appends an
  entry, creates its index/history/aggregate records, and sifts upward.
- Cancel locates the order immediately, swaps it with the last entry, updates
  both position records, pops the vector, erases its live record, and subtracts
  its aggregate. A zero total removes that price record. If a replacement
  remains, it sifts upward when better than its parent, otherwise downward.
  There are no tombstones, lazy deletion or stale heap entries.
- Every heap swap updates both affected ID positions. Vector growth changes
  addresses but not indexed positions; no pointers into vector storage are kept.
- BBO reads each heap root's price and the corresponding aggregate total, or
  returns an absent optional side. No scan over same-price orders is needed.
- Bid/ask depth copies only occupied aggregate levels, sorts by price best-first,
  and truncates to depth. A nonpositive request returns before allocation.
  Even depth 1 copies/sorts all occupied levels; returned snapshots own their data.

### Allocation ordering and exception safety

All validation and sequence-exhaustion checks happen before mutation. An Entry
contains only trivial scalar fields, so vector push_back can grow with its strong
exception guarantee. If that succeeds, no heap movement occurs yet. The code then:

1. Creates/finds an aggregate record (a new one initially has total zero).
2. Inserts the accepted-ID record.
3. Inserts the live ID/position record.
4. Commits the aggregate quantity and increments the sequence, then sifts up.

If steps 1-3 throw, completed history/aggregate insertions are erased and the
provisional vector entry is popped. If vector growth itself throws, no index
has yet changed. Existing aggregate totals and next sequence are not modified
until all allocations complete. No further allocations occur during commit or
sifting: Entry swaps are trivial, integer arithmetic is prechecked and existing
position records use find rather than insertion. Fixed integral hash/equality
operations do not throw. Cancellation likewise needs no allocation.

This strategy is intended to preserve logical state on allocation failure;
vector capacity and hash bucket counts may remain larger. Allocation failure
has not been injected deterministically in the tests, so exception-path coverage
is a known limitation, not a fault-injection-verified guarantee. Query allocation
failure leaves the book unchanged. Internal logic_error checks indicate an index
invariant bug, not a normal user-input error.

Sequence values 0..UINT64_MAX-1 are available. Once next_seq_ reaches UINT64_MAX,
a business-valid new order throws overflow_error before mutation or ID consumption;
there is no wraparound or renumbering. Business-invalid adds still return their
normal validation result first. Sequence exhaustion is tested via friend access.
A uses list order and has no equivalent sequence limit.

### B invariants and deterministic tests

The B test-access struct is defined only in verify.cpp. It independently checks
heap ordering, unique arrival sequences, every ID-to-position locator, legal
order fields, permanent history membership, heap/live counts and recomputed
per-price totals against the aggregate hash tables. FIFO IDs are reconstructed
by sequence, and explicit root-draining tests verify tie priority itself.

B verification includes the original shared contract and heap-specific cases: the original 20 A contract cases, the same
20 applied to B, seven B-specific cases and one deterministic A/B cross-check.
The B-specific cases cover root/downward repair, middle/upward repair,
middle/downward repair, last-element removal, FIFO root order, 256-order
repeated-swap/growth/locator consistency, and sequence exhaustion. Each applicable
repair fixture runs on both sides. Overflow tests retry the rejected ID and check
unchanged snapshots/history. All runtime checks remain enabled under NDEBUG.

The A/B cross-check compares add/cancel results, BBO presence/prices/quantities
and both depth snapshots at depths -1, 0, 1, 3 and INT64_MAX after every operation.
It covers invalid input, overflow/retry, cancelled-ID rejection, repeated prices,
unknown/repeated cancellation and complete draining. The separate reference-model tests provide additional verification.
## C: Tick arrays and occupancy bitmap

`impl_c/orderbook_c.hpp` and `.cpp` define `OrderBookC`, independently of A/B.
Each side has a `std::array<Level, 5000>`. Every level owns a cached signed 64-bit
aggregate and a FIFO `std::list<Order>`. Each side also has a separate
`std::array<uint64_t, 79>` bitmap. Empty level records remain allocated for the
book lifetime; there is no tree, heap, or sorted-vector price index.

`live_` maps ID to side, fixed array index and list iterator. `used_ids_`
permanently retains accepted IDs. Array locations and list nodes are stable;
rehashing ID indexes does not invalidate stored node iterators. Copy and move
are disabled. No caller timestamp or arrival sequence is needed: FIFO is list
insertion order, with no sequence-exhaustion condition.

### Tick and bitmap indexing

After validating side and inclusive price boundaries, bid index is `price - 5000`
and ask index is `price - 10001`. Both produce 0..4999. Word index is `index / 64`
and bit position is `index % 64`; masks always use unsigned `uint64_t{1}`.
Bits 0..7 of word 78 represent indexes 4992..4999. Its remaining 56 bits are
invalid and always remain zero during normal operation. Query traversal also
masks this word with 255 before selecting a bit, so an unused bit cannot address
an invalid level. Other words have all 64 bits available.

Bids scan words from 78 down to 0 and select the highest set bit. Asks scan from
0 up to 78 and select the lowest set bit. `bit_index` uses portable shifts and
masks at widths 32, 16, 8, 4, 2 and 1, without C++20 functions or compiler-specific
intrinsics. It is called only with nonzero words; no shift reaches 64. Each
selected index is converted back to a tick by adding its side's minimum price.

### Five operations

1. Add: validate all business inputs, find the direct level, check aggregate
   overflow, append a FIFO node and insert history/live records. Only after all
   allocations succeed, update the total and set the occupancy bit.
2. Cancel: use the locator to subtract quantity, unlink the node and remove its
   live record. Clear the bit when the list becomes empty; the fixed record
   remains with total zero. Accepted-ID history is never removed.
3. BBO: scan each bitmap for its best index and copy that level's cached total.
   This allocates no snapshot and handles each empty side independently.
4. Bid depth: scan high words/bits first, copy each occupied price/total, and stop
   at the requested count. No individual-order scan or sorting is performed.
5. Ask depth: scan low words/bits first with the same owned snapshot behavior.
   Nonpositive depth returns immediately; oversized depth returns all levels.

### Allocation failures and ID rules

List-node allocation happens before either index mutation. If it fails, there
is no state change. After a successful append, insertion of the permanent
accepted-ID record and then the live locator may allocate. If either fails,
completed history insertion is erased and the appended node is popped. Totals
and bits are committed last using nonthrowing arithmetic/bit operations.
`std::bad_alloc` propagates. Bucket capacities may remain enlarged, but logical
book contents are intended to be restored. Allocation failure is not injected
by the suite, so these rollback paths remain code-reviewed rather than
fault-injection verified. Snapshot allocation failure does not mutate the book.

Validation precedence, overflow checks, rejected-ID retry, permanent ID history,
unknown cancellation and ID zero/UINT64_MAX follow A's shared contract exactly.
There is no level-wide or book-wide scan for cancellation, and no allocation
on successful cancellation.

### C invariants and verification

Test-only access checks all 10,000 fixed level records against bitmap occupancy,
zero totals for empty levels, positive totals for occupied levels, FIFO-node sums,
side/tick legality, each live locator, node/live-index counts, permanent history
membership and unused final-word bits. Explicit FIFO expectations check list
ordering. Production queries never perform this full validation scan.

The verifier contains 20 contract cases per implementation, seven B-specific
cases, nine C-specific cases and two cross-checks: 78 cases total. C-specific coverage:
endpoints/first-last bits; 63/64, 127/128 and 4991/4992 boundaries; all eight final
word bits plus defensive masking of deliberately poisoned unused bits; best-price
changes and empty transitions; FIFO node cancellation and bit lifetime; overflow
and ID rejection; sparse aggregated depth/snapshot stability; 256-order locator
mutations; and bit-selection checks across every bit position.

The A/B/C sequence compares operation results, BBO flags/prices/totals and both
depth snapshots at -1, 0, 1, 3 and INT64_MAX after each operation, while checking
all three internal invariant sets. It includes invalid requests, overflow/retry,
ID reuse rejection, word-spanning prices, UINT64_MAX, repeated cancellation and
complete draining. It is deterministic correctness verification, not a benchmark
or a substitute for the separate independent reference model described below.
## Expected complexity and limitations

N = live orders; U = lifetime accepted IDs; L = occupied levels on the relevant
side; K = min(positive depth, L); P = 5,000; W = ceil(P/64) = 79 per side.
Hash costs below are expected amortized, not hard latency guarantees.

| Operation | A: implemented | B: implemented | C: implemented |
| --- | --- | --- | --- |
| Add | O(log(L+1)) | O(log(N+1)) | O(1) |
| Cancel | O(1) amortized | O(log(N+1)) | O(1) |
| BBO | O(1) | O(1) expected | O(W) |
| Positive depth including output | O(K) | O(L log(L+1)) | O(W + K) |
| Nonpositive depth | O(1) | O(1) | O(1) |
| Persistent storage | O(U + N + L_bid + L_ask) | O(U + N + L_bid + L_ask) | O(U + N + P) |
| Additional depth storage | O(K) | O(L) | O(K) |

A's map erase by iterator is amortized constant, not a guarantee for every call.
Its list and map nodes allocate separately and cause pointer chasing. Hash
collisions can make live lookup O(N) and history lookup O(U); rehashing and
allocation cause latency spikes. Permanent history memory grows even as orders
are cancelled. B may also reallocate its heap vector and sort all occupied levels
on shallow depth requests. Each heap swap performs two locator hash lookups;
adversarial collisions can make a full repair O(N log(N+1)), and a history rehash
can cost O(U). Vector capacity is retained after cancellations. No performance numbers
are inferred from these bounds.

## Limitations

C has O(P) construction work and fixed storage even when empty; destruction also
visits fixed levels and releases live nodes and ID records. Its FIFO nodes still
allocate. Hash worst cases and permanent history growth apply to every design.
None is a custom-pool or allocation-free implementation. B's sequence-exhaustion
policy is explicit above; A/C use list insertion order without a sequence counter.

Allocation-failure injection, exhaustive state exploration, independent sparse-ID
random traces, production tail latency and controlled native Linux measurements
are not covered.
## Independent randomized verifier

verify.cpp retains all 78 deterministic cases and adds --random, --ops and --seed.
The defaults are 100000 primary operations and seed 42. Each random invocation
first runs the deterministic suite, including business-input rejection, empty
books, boundary prices, overflow, duplicates, cancelled-ID reuse and unknown
cancellation cases with explicit independent expected values.

The random oracle is an append-only vector of {Order, active} records, indexed
by dense monotonic trace ID. Cancel marks a record inactive without deleting its
accepted-ID history. Each side's aggregate levels are kept in an ascending sorted
vector using lower_bound plus insertion/erasure; this is deliberately different
from A's maps, B's heaps/hash aggregates and C's bitmap/arrays. The oracle uses
none of their internal accessors, locators or ordering algorithms. Internal
accessors are used only for periodic production invariant checks.

The oracle is scoped to sequential IDs from this generated workload; sparse and
UINT64_MAX IDs remain covered by the deterministic contract tests. It is not a
new production implementation. Its allocation failures abort the verification
run; recovery from oracle allocation failure is not promised.

mt19937_64 plus explicit rejection sampling maps draws uniformly to bounded
integers without depending on uniform_int_distribution library behavior. Choices
0..69 add, 70..89 cancel and 90..99 query BBO. When a cancellation is selected with
no live IDs, it becomes an add. An independent live-ID vector supports uniformly
selecting a live cancellation and swap/pop removal; only accepted IDs enter it.
Adds sample bid/ask with equal probability, prices across 5000 ticks uniformly,
and quantities 1..100. Actual operation/side counts are printed, not assumed.

After every operation the verifier compares enum results as applicable, BBO
presence/price/aggregate, and both depth(10) snapshots against the model. It also
checks initial empty state, full snapshots and all production invariants every
1000 operations and at a final nonmultiple checkpoint. At those checkpoints the
model independently reconstructs totals by sorting its active order records and
summing equal prices, checking its incremental aggregate vectors and live count.

This avoids scanning all live orders for every operation; invariant/oracle scans
are periodic. The verifier still sorts B's occupied levels for each depth query,
so it is intentionally more expensive than a benchmark. Runtime includes oracle,
comparisons and invariant work. It does not report performance claims.

Mismatch diagnostics include seed, operation index, details, implementation and
expected/observed result or first differing level/size. Unexpected production
exceptions also identify the responsible implementation. Main returns nonzero
on verification or CLI errors. Numeric parsing rejects malformed, negative or
out-of-range uint64 values; this verifier is separate from the benchmark executable.

Finite seeds do not prove correctness. Random operations are valid and use small
quantities; the deterministic cases provide invalid-input and overflow coverage.
Allocation fault injection, large-scale sparse-ID random traces, and exhaustive
state exploration are not covered. See README for executed commands
and observed results.
