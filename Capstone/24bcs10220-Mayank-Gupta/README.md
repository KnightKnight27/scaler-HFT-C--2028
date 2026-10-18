# Resting order book

A C++17 comparison of three order-book designs for one instrument. Orders are
added and cancelled; there is no matching, trading, networking or concurrency.

Name: Mayank Gupta  
SST email: mayank.24bcs10220@sst.scaler.com  
Roll number: 24bcs10220

## Build and run

Only a C++17 compiler and Make are needed. No third-party C++ libraries.

In the course repository, first enter `Capstone/24bcs10220-Mayank-Gupta`.

```sh
make                         # -std=c++17 -O2, with debug symbols for perf
./bench --verify              # edge cases, then 20,000 random operations
./bench --impl A              # 1,000,000 operations, five runs
./bench --impl B
./bench --impl C
make sanitize                # separate AddressSanitizer + UBSan build
```

Equivalent standalone build command:

```sh
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic \
    -Isrc -Itests src/bench.cpp tests/verify.cpp -o bench
```

On macOS, `/usr/bin/g++` is Apple's Clang. For a Linux GCC build, use
`make CXX=g++`. After changing compilers or compiler flags, run `make clean` first.

## Interface and rules

Each class has the same five public methods; there is no base class or virtual
dispatch. The benchmark selects the class once, before replaying the workload.

```cpp
void add_order(OrderId id, Side side, Price price, Quantity qty);
bool cancel_order(OrderId id);
BBO get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

Changes from the assignment interface:

- Prices are integer ticks throughout, including returned levels and BBOs.
  `9950` means `99.50`. Buy prices are `5000..9999`, sell prices `10001..15000`.
- Quantities use `std::int64_t` instead of `long` so their width is explicit.
- Unknown or already cancelled IDs return `false`. Cancelling removes the whole order.
- Empty sides have a false presence flag and zero price/quantity fields.
- Bid levels are descending; ask levels are ascending. Only occupied levels appear.
  Depth counts price levels, not orders. Nonpositive depth returns an empty vector.
- Invalid side/price, nonpositive quantity, or duplicate live ID throws
  `std::invalid_argument`. Level-quantity overflow throws `std::overflow_error`.
  These rejected adds leave the book unchanged.
- IDs may be arbitrary `uint64_t` values, including zero. As required by the brief,
  the caller must never reuse a previously used ID. Cancelled IDs are not retained
  just to enforce this lifetime rule; the workload uses monotonically increasing IDs.

## The designs

| | A: `MapBook` | B: `VectorBook` | C: `ArrayBook` |
|---|---|---|---|
| Individual orders | `map<ID, Order>` | `unordered_map<ID, Order>` | Packed `vector<StoredOrder>` |
| ID lookup | Ordered-tree lookup | Hash lookup to order | Hash lookup to vector slot |
| Price totals | Two `map<Price, Quantity>` trees | Two sorted vectors of levels | Two arrays of 5,000 quantities |
| Best price | Tree endpoints | Vector endpoints | Cached occupied indices |
| Cancellation | Find order, find level, subtract, erase | Find order, binary-search level, subtract; erase empty level | Find slot, subtract by price index, swap last order into hole, repair moved ID |

A is the baseline. B makes price levels contiguous but pays for shifting elements
when a level appears or disappears. C takes advantage of the fixed price bands.
It scans for the next occupied price only when its best level becomes empty.
C's hash table still uses allocated nodes; packed order records do not make the
whole structure contiguous or allocation-free.

No design reserves space using future workload information. Normal container
growth, rehashing and allocations are included in operation timings.

### Complexity

Let `N` be live orders, `L` occupied levels on the affected side, `P = 5000`
possible levels per side, and `K = min(max(depth, 0), L)`.

| Operation | A | B, expected/amortized hash costs | C, expected/amortized hash/vector costs |
|---|---|---|---|
| Add | `O(log N + log L)` | `O(log L)` existing level; `O(L)` new level | `O(1)` |
| Cancel | `O(log N + log L)` | `O(log L)` retained level; `O(L)` removed level | `O(1)` normally; `O(P)` if best-price scan needed |
| BBO | `O(1)` | `O(1)` | `O(1)` |
| Bid depth | `O(K)` | `O(K)` | `O(P)` worst case |
| Ask depth | `O(K)` | `O(K)` | `O(P)` worst case |

Empty cases are constant time. For C's depth queries, the scan stops once enough
occupied prices are found, so dense top-of-book reads usually visit about `K`
entries. Keeping `P` in the table exposes sparse-book scan costs even though the
assignment fixes its value.

Hash lookups have `O(N)` worst-case behaviour. Individual hash insertions or
vector growth can be linear; amortized constant time is not a latency bound.
B's overall add/cancel worst case is `O(N + L)`; C's add worst case is `O(N)`
and cancel worst case is `O(N + P)` under the usual container complexity model.
A/B use `O(N + L)` live storage; C uses `O(N + P)`. Vector/hash capacity may be
retained after cancellations, so actual allocated memory follows peak occupancy.
Returned depth vectors allocate their own storage in every design.

## Correctness

`--verify` checks hand-written cases with known answers, then replays one generated
workload through all three implementations and an independent reference model.
The reference keeps a simple list of orders, cancels by linear search, and
rebuilds price totals from scratch.

After **every** random operation, verification compares BBO and **all** bid/ask
levels with the reference. Every 100 operations it also checks truncated, zero,
negative and oversized depth requests. It then drains the book, checking results
after each attempted cancellation. Failures return a nonzero exit status.

```sh
./bench --verify --ops 20000 --seed 7
./bench --verify --ops 1000000 --seed 42  # optional, deliberately expensive
```

The default verification workload is smaller than the benchmark because exhaustive
snapshots and rebuilding an independent reference are expensive. Both modes use
the same generator. Verification is never inside a timed benchmark.

## Benchmark method

- Generate the workload once with `std::mt19937`, default seed 42, before timing.
- Roughly 70% add, 20% cancel and 10% BBO. Buy/sell is 50/50, quantities 1..100,
  and valid ticks are uniformly distributed within each side's band.
- The generator keeps its own live-ID list. Cancels select from that list.
  An attempted cancellation with no live IDs becomes an add.
- Call both depth APIs with depth 10 after every 100 primary operations.
- Replay identical operations into a fresh book five times. Each metric reports
  its own median. Repeated runs must have the same checksum and final BBO.
- `steady_clock` surrounds each add, cancel and BBO call. The total interval
  includes replay dispatch, per-operation timing, depth queries and checksum work.
  It excludes generation, initial book construction, final inspection, printing,
  and book destruction. Allocations *inside operations* remain measured.
- Query outputs and cancellation results feed a printed checksum.

Clock overhead is not subtracted. Nanosecond-scale BBO numbers include substantial
measurement overhead and should not be read as isolated hardware latency. No CPU
pinning or cold-cache guarantee is applied. Fresh books can reuse the allocator's
previously freed memory. No tail-latency or peak-memory claim is made.

The seed gives repeatable workloads within a build/environment. C++ standard
library implementations can map random distributions differently: compare A/B/C
within the same compiler/library environment, not a Mac run against a Linux run.

```sh
./bench --impl C --ops 1000000 --seed 42 --runs 5
./bench --impl C --profile
```

`--profile` runs one replay with per-operation clock calls compiled out. It keeps
the same operations, depth queries and checksum. It prints only a replay total,
not per-operation averages. This is the mode used for profiling.

## Linux perf

Build on Linux first. The profiling script records the requested counters and
call graphs for each implementation:

```sh
sh scripts/profile.sh
```

If hardware counters are unavailable in a VM, retain the failed/unsupported
output and rerun with available events in a separate directory:

```sh
EVENTS=task-clock,page-faults,context-switches,cpu-migrations \
RECORD_EVENT=cpu-clock OUT=results/linux-software sh scripts/profile.sh
```

`perf stat -r 5` reports counter means and variation; the benchmark's timing rows
are medians of five per-run averages. Whole-process perf includes generation,
construction, destruction and printing too. Its task-clock is not the same scope
as the internally measured replay total. Call graphs help distinguish that work.

Use `perf list` to inspect supported events. Permissions may also restrict perf;
document that separately from unsupported hardware. Never fill unavailable IPC,
branch-miss or cache metrics with zero or inferred numbers.

### Linux on the development Mac

A local Lima VM named `orderbook` was created for these measurements: Ubuntu 26.04,
native ARM64, Apple's VZ backend, two vCPUs, 4 GiB RAM and a 12 GiB virtual disk.
It has no host-directory mounts. Sources are copied to `~/orderbook` inside the
guest. This is a VM benchmark, not a bare-metal trading-server benchmark.

To resume the existing VM and enter its shell:

```sh
limactl start orderbook
limactl shell --workdir=/tmp orderbook
cd ~/orderbook
make CXX=g++
./bench --verify
./bench --impl A
sudo env RECORD_EVENT=cpu-clock sh scripts/profile.sh
exit
limactl stop orderbook
```

The guest's default perf permissions require `sudo`. Hardware counters are not
exposed; the saved stat files explicitly show `<not supported>`. Software-clock
sampling supplies call graphs. The guest's perf permission setting was not changed.

To copy updated sources from the Mac, run these from the repository root:

```sh
mkdir -p build
COPYFILE_DISABLE=1 tar --no-xattrs -czf build/orderbook-source.tar.gz Makefile src tests scripts
limactl copy build/orderbook-source.tar.gz orderbook:/tmp/orderbook-source.tar.gz
limactl shell --workdir=/tmp orderbook sh -lc \
    'mkdir -p "$HOME/orderbook"; tar -xzf /tmp/orderbook-source.tar.gz -C "$HOME/orderbook"'
```

For another Mac, install Lima with `brew install lima`, then create the guest:

```sh
limactl start --name=orderbook --vm-type=vz --cpus=2 --memory=4 --disk=12 \
    --mount-none --containerd=none --image-variant=minimal --tty=false template:ubuntu
limactl shell --workdir=/tmp orderbook sh -lc \
    'sudo apt-get update && sudo apt-get install -y g++ make linux-tools-common linux-tools-$(uname -r)'
```

## Files

- `src/`: three books, common types, workload generator, benchmark entry point.
- `tests/verify.cpp`: edge cases and reference-based verification.
- `results/`: actual execution logs and environment details.
- `REPORT.md`: measured comparison and limitations.
- `scripts/profile.sh`: repeatable Linux profiling commands.

The report uses only the Linux dataset. `results/macos/` is a separate native
comparison; it uses libc++ and therefore has a different generated sequence.
Binary perf recordings stay in the VM's `~/orderbook/results/linux-perf/` and are
excluded from Git; their text reports are included here.

