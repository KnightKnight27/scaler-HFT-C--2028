# Design decisions (written before implementation)

The supplied PDF is authoritative: a single-instrument, single-threaded resting
book, no trades or matching. The older Capstone/capstone.txt lecture notes describe
a different exercise. Submission deadline is 17 October 2026, 11:59 PM; PDF does
not specify a timezone.

A: ascending std::map<int,int64_t> for each side; std::unordered_map<uint64_t,Order>
for live orders. Best bid is reverse begin; best ask is begin. Cancel hashes the
ID, then finds/decrements/removes its tree level.

B: ascending vector<Level> for each side; std::map<uint64_t,Order> for live orders.
Binary search locates a level. New/removed levels shift vector elements. Best
prices are vector endpoints. Cancel uses the order tree and repeats price lookup;
no pointers into vectors survive mutation.

C (price-domain design): two fixed arrays of 5,000 aggregate quantities, and
79 64-bit occupancy words per side. Cached best indices make BBO constant time;
portable word scans find a new best when necessary. A packed vector<Order> stores
live orders and an unordered_map<ID,size_t> points to slots. Cancel swap-removes
an order and repairs the moved ID's slot. No custom hash table or tombstones.

All start empty without workload-sized reserves. Shared validation accepts integer
cent ticks and positive int64 quantities, rejects invalid side/band/quantity and
live duplicate IDs, and checks aggregate overflow. Historical ID uniqueness is a
caller precondition; cancelled IDs are not retained. Arbitrary uint64 IDs work.

Verification uses independent std::map<ID,Order> plus rebuilding sorted aggregates
at bounded-suite checkpoints, and independently maintained reference aggregates
for the million-operation run. Compare cancellation, BBO and top-10 after every
primary operation; compare full depth after every bounded operation and every
10,000 operations plus the final state in the long run. Mandatory depth calls
also occur every 100 primary operations.
