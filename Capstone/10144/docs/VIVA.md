# Explanation and viva guide

**What is this book?** A table of resting buy/sell quantities. It never trades.
Buy 99.99 is valid; sell 100.01 is valid; 100.00 is excluded. A high bid and low ask
are independent and either side may be empty.

**Why cents?** Floating-point cannot represent every decimal fraction exactly.
The integer 10001 means 100.01. Buy index = cents − 5000; sell index = cents −
10001, always 0..4999 after validation. One array per side has 5000 entries.

**Two jobs:** find a live order by ID, and maintain its price's total. Cancel looks
up its exact quantity and subtracts only that quantity. Remove the price only
when its total reaches zero. Unknown IDs return false; they do not create entries.

**A:** an unordered map finds orders. A map keeps each side's price totals sorted.
The last bid and first ask are best. Traversing the tree in price order produces
depth. Each live order occupies a separate hash node; each occupied tick a tree
node. Cancellation does not need a queue or order matching.

**B:** a map finds IDs. Price levels sit next to each other in sorted vectors.
Binary search finds a price. Inserting/removing a price shifts later entries.
Never save a pointer to a vector element across mutation: growth or shifting can
invalidate it. Existing-level updates are cheap and best prices are endpoints.
Sequential generated IDs favor ordered tree insertion patterns; this workload
is one distribution, not a universal production conclusion.

**C:** arrays directly map ticks to quantities. A bit means a tick is occupied.
Index i uses word i/64 and bit i%64; a uint64_t 1 is shifted by only 0..63.
Only 5000 bits are set, so padding in the last word cannot generate an index.
Best indices are cached. If the best empties, scan words from the relevant end,
then find a set bit. Depth scans words and consumes bits from a local copy.
Portable bit search shifts at most 63 times; it is not magic free constant work.
Orders are packed in a vector and a hash maps IDs to slots. Cancel copies the
last order into the hole and fixes that moved order's slot before removing the
cancelled ID. No vector pointer is stored. Vector growth is a real occasional
O(N) cost; the fast average does not promise bounded worst-case latency.

**Overflow?** Quantities are positive signed 64-bit integers. Before addition,
check extra > INT64_MAX − current. Subtraction cannot go negative because each
cancelled order is live and included in that aggregate. Failed additions must
leave no live-ID entry. IDs span all uint64 values; never compute ID-sized arrays.
Caller promises IDs are never reused. Remembering every old ID would cost history
rather than live state, so the books intentionally do not do that.

**Complexity?** Hashing is expected O(1), worst O(N), with occasional rehash.
Tree operations take O(log N) or O(log L). B shifts O(L). C's best-removal scan is
O(W+64), W=79, and depth O(W+64K); it is only bounded because prices are bounded.
Returned depth itself costs O(K) space. Cleared vectors and hash buckets retain
capacity, which follows peak live orders rather than only current live orders.

**How is correctness checked?** Every primary step compares cancel results, BBO
and top-10 for all books versus an independent reference. Full-depth checks occur
after every bounded-suite operation, and every 10000 plus final in the million
run. Reference live orders independently rebuild price sums. Edge tests include
all ticks, extremes, empty books, partial cancellations, rejected input and overflow.
Sanitizers catch memory/undefined-behavior problems, but do not prove correctness.

**What is actually timed?** Generate the entire trace first with a separate live-ID
model. Choose cancels uniformly from current live IDs by vector indexing and
swap-removal. Use the same trace/hash for each book. One million primary calls,
plus two depth calls each 100 steps. Per-call clocks measure operation averages;
total also includes clocks, checksum and depth calls. Construction/destruction
are excluded internally. Five fresh measured books with consistent warmup and
one pinned CPU; median each metric independently. Clock overhead can dominate
BBO. Bulk mode is a separate total-only experiment, not mixed into the main table.

**Why checksums?** Fold query results into an unsigned hash and print it and final
BBO. This makes the results observable and detects divergent output. Verification
is still stronger than the checksum, which is not a collision-free proof.

**What does perf tell us?** IPC=instructions/cycles; branch miss percentage=100×
misses/branches, with the same event domain and collection scope. Hardware events
are hypotheses' evidence, not synonyms for execution time. More IPC can still
mean a slower program if it executes many more instructions. Cache misses depend
on access pattern and allocation. Page faults count page population/faults, not
bytes of total memory. A profile identifies sampled hot symbols, not exact costs.
External perf includes generator, clock and allocator work; do not call all of
its counters book operations. Hybrid core events must match the pinned CPU.

**Production?** Read report for measured choices. Consider worst-case growth,
allocation, cache footprint, arbitrary price ranges, maintainability and realistic
workloads before choosing. No tail latency or memory-usage experiment was done.
A fixed price domain makes C practical; a changing/unbounded domain can favor A.
B's vector shifts are especially bad if occupied levels churn. Next test reserved
capacity, pools, wider workloads, compact bit-position search and p99 latency.

**Originality and understanding:** source was developed for this submission with
coding-agent assistance, without copying another student's implementation. This
document makes no claim about an unstated course AI policy. Read the small headers,
then generator, replay loop and verifier, and be prepared to explain each line.
