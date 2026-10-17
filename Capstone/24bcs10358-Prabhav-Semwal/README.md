# Low-Latency Order Book

Name: Prabhav Semwal

```text
email: "prabhav.24bcs10358@sst.scaler.com"
roll_no: "24bcs10358"
```

Single-instrument resting order book, C++17, standard library only, single-threaded. Orders are added or cancelled; quantities are aggregated by price. There is no matching or trade generation.

## Build and run

From the repository root:

```bash
cd Capstone/24bcs10358-Prabhav-Semwal
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic bench.cpp order_book.cpp -o bench
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
```

`make` builds the same executable. Each benchmark defaults to five runs of 1,000,000 operations and prints the median of each measurement, all trial results, the final BBO and a checksum. `--ops N` and `--runs N` allow smaller development runs. `--profile` replays the same workload once without per-operation clock calls; its CSV latency fields are zero placeholders, not measured latencies.

## Files

| File | Purpose |
| --- | --- |
| `order_book.h` | Common types and declarations of the three books |
| `order_book.cpp` | The three implementations and C's custom order-ID table |
| `bench.cpp` | Workload generator, correctness checks and benchmark CLI |
| `Makefile` | Build, verify and clean targets |
| `profile.sh` | Linux measurements and call-stack collection |
| `Dockerfile` | Optional Linux build/profiling environment |
| `REPORT.md` | Results table and performance analysis |
| `results/` | Actual correctness, timing, perf and memory logs |

## API and prices

All three books expose the same methods:

```cpp
void add_order(uint64_t id, Side side, int price, long qty);
bool cancel_order(uint64_t id);
BBO get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

The interface change is **integer ticks instead of doubles**, including the price fields of `Level` and `BBO`. For example, 99.99 is passed and returned as `9999`; 100.01 is `10001`. Bids accept ticks 5000–9999 and asks accept 10001–15000: exactly 5,000 valid levels per side. Tick 10000 is excluded. The class names are `MapBook`, `VectorBook` and `ArrayBook`.

Quantities must be positive and aggregate quantities must fit in `long`. Order IDs are arbitrary `uint64_t` values, including 0 and `UINT64_MAX`, and must never be reused, as required by the assignment. Duplicate live IDs, invalid sides, out-of-band ticks and nonpositive quantities throw `std::invalid_argument`. The books do not retain historical IDs to police reuse after cancellation.

Unknown or already-cancelled IDs return false. Empty sides have their corresponding BBO flag set to false. Bids are returned highest-first and asks lowest-first; depth queries return only available levels. Zero or negative depth returns an empty vector.

## Three designs

| | A: MapBook | B: VectorBook | C: ArrayBook |
| --- | --- | --- | --- |
| Orders | `std::map<ID, Order>` nodes | `std::unordered_map<ID, Order>` nodes | Custom open-addressed table in a contiguous vector |
| Price levels | Two `std::map` trees | Two sorted vectors, both ascending | Two fixed arrays of 5,000 quantities |
| Best price | First entry of each side's tree | Last bid / first ask vector element | Cached best array indices |
| Cancellation | Order-ID tree lookup, then level tree lookup | Hash lookup, then binary search for the level | Linear-probing ID lookup, then direct tick indexing |

C hashes the ID into a power-of-two table and probes successive slots on collisions. Slot states are empty, live and deleted. A cancelled slot becomes a tombstone so searches can continue to collided orders. At a 70% occupied-or-deleted threshold the table doubles and reinserts live orders. Price quantities use `price - 5000` or `price - 10001` as the array index. When the best level becomes empty, a bounded scan locates the next best level.

### Complexity

Let N be live orders, L active levels on one side, D returned levels, P = 5,000 possible ticks per side, and H allocated slots in C's ID table.

| Operation | A | B, expected/amortized | C, expected/amortized |
| --- | --- | --- | --- |
| `add_order` | O(log N + log L) | O(log L), plus O(L) when a new level shifts the vector | O(1) |
| `cancel_order` | O(log N + log L) | O(log L), plus O(L) when an empty level is erased | O(1), plus up to O(P) if the best level disappears |
| `get_bbo` | O(1) | O(1) | O(1) |
| `get_bids` | O(D) | O(D) | O(P) worst case, skipping empty ticks |
| `get_asks` | O(D) | O(D) | O(P) worst case, skipping empty ticks |
| Space | O(N + L) | O(N + L) | O(H + P) |

B's hash lookup can degrade to O(N); C's probing or a table growth can take O(H). Neither hash design guarantees constant worst-case add/cancel latency. C counts tombstones toward its growth threshold, so H depends on insertion/deletion history as well as live orders. Its price arrays alone occupy 80,000 bytes on this 64-bit platform.

## Workload and measurement

The generator uses `std::mt19937(42)` with integer remainder-based choices. It builds one trace before timing and maintains a separate vector of live IDs; cancellation picks one and removes it by swapping with the last element. IDs increase and are never reused. The final trace contains 700,829 adds, 199,496 cancels and 99,675 BBO queries. Sides are approximately 50/50, quantities are 1–100, and prices are valid random ticks. Every 100 operations, both depth-10 queries run, giving 10,000 calls per side.

All books replay that exact trace. Each operation category is timed separately with `std::chrono::steady_clock`. Its average is accumulated nanoseconds divided by its call count; the reported value is the median of five trial averages. Timing brackets include clock overhead, which matters for the very short BBO call. Checksum mixing is outside each operation's timing bracket. Total time includes replay, timing overhead, checksum updates and depth queries, but excludes workload generation, initial book construction, final full-depth checksumming and destruction. The final state and query/depth checksums are printed and checked across trials.

## Correctness

`--verify` checks empty books, unknown/repeated cancellation, aggregation, ordering, insufficient/nonpositive depth, boundary ticks, one-sided books, invalid inputs, duplicate live IDs, and IDs 0/`UINT64_MAX`. A separate 2,000-operation reference test rebuilds levels from live orders and compares full depth after every operation. The million-operation differential test compares BBO and top-ten levels after every operation, plus full depth every 10,000 operations and at completion.

Actual output is in [results/verify.txt](results/verify.txt):

```text
Implementation A: PASS
Implementation B: PASS
Implementation C: PASS
All implementations produce identical results.
```

The full verification also passed under AddressSanitizer and UndefinedBehaviorSanitizer on macOS; output is in [results/sanitizer-verify.txt](results/sanitizer-verify.txt).

## Profiling on Linux

The recorded runs used this machine's Colima Linux VM: Apple M5 host, 8 virtual ARM64 CPUs, Linux 6.8.0-117, Debian 12 container, GCC 12.2.0 and perf 6.1.190. The process itself is single-threaded. Source hashes and full environment output are in [results/environment.txt](results/environment.txt).

On Linux with perf installed and permitted:

```bash
sh profile.sh
```

This rebuilds with `-O2 -g -fno-omit-frame-pointer`, verifies correctness and saves five-run medians. For each implementation it runs the requested events with `perf stat -r 5` against `./bench --impl A --profile` (then B and C). Each process replays one million operations; perf therefore repeats five workloads rather than five sets of five workloads. Process-wide perf counts include workload generation, construction, teardown and output, unlike the replay timer.

Equivalent individual commands:

```bash
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A --profile
perf record -F 9999 -e task-clock:u -g -o perf-A.data ./bench --impl A --profile
perf report --stdio -i perf-A.data
```

Repeat for B and C. Software task-clock sampling is used because hardware cycles are unavailable here. Raw stat output is in `results/perf-stat-A.txt` through `C`; record logs and readable call graphs are in `results/perf-record-*.txt` and `results/perf-report-*.txt`. `/usr/bin/time -v` outputs are also included. Binary perf files are left in `/tmp` by the script and are not committed.

This VM supports task-clock and page-faults. It reports cycles, instructions, branches, branch-misses, cache-misses, L1-dcache-load-misses and dTLB-load-misses as **not supported**. The report marks those metrics and derived IPC/branch-miss rates N/A. They require a Linux machine exposing suitable hardware counters.

For a reproducible container setup on a Linux-capable Docker host:

```bash
docker build -t prabhav-orderbook .
docker run --rm --cap-add PERFMON --security-opt seccomp=unconfined -v "$PWD:/work" -w /work prabhav-orderbook sh profile.sh
```

## References

Submission layout and reporting examples reviewed: [PR 141](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/141), [Deepesh's PR 158](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/158), [Minesh's PR 144](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/144), and [Ansh's PR 166](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/166). The source, workload and measurement logs in this folder were produced for this submission; no classmates' benchmark or perf numbers are used.
