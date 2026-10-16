# Low-latency order book

**Mohit Kumar · roll 10222 · mohit.24bcs10222@sst.scaler.com · GitHub mohit-1710**

Solo submission; no teammate. AI assisted the implementation, testing and analysis.
This does not claim independent authorship. The [official brief](https://docs.google.com/document/d/1lxzuXeMuJiiMVDiJKfag4BKyBqwndyOFf7BSIOJNAG4/edit)
requires student-owned implementation and the ability to explain every line;
that assessment condition still applies.

The three books expose the same five operations for one instrument, with no
matching or trades. The [two-page report](docs/analysis.pdf) contains the completed
results table, branch/cache/fault analysis and production choice.

## Build and run

From this folder with GCC and Make installed:

```sh
make CXX=g++                 # C++17, -O2, debug symbols and frame pointers
./build/verify               # edge/layout tests + 20,000 events, seed 42
./build/verify 1000000 42     # full-depth comparison after every event
./build/bench --impl A       # 1,000,000 primary events, five repetitions
./build/bench --impl B
./build/bench --impl C
make sanitize               # optional Clang ASan/UBSan check
```

Only the C++ standard library is required. Full-depth verification deliberately
costs much more than replay. On the measured Mac, GCC 16 is selected with
`make CXX=g++-16`; Apple Clang is also validated.

## Interface and behavior

Prices use **integer ticks instead of double** in `add_order`, `Level` and `BBO`:
99.99 is `9999`; 100.01 is `10001`. Other logical fields and the five method names
are unchanged. `get_bids`/`get_asks` default to ten levels.

- Bid ticks are `5000..9999`; ask ticks are `10001..15000`. Tick `10000` is excluded.
- Quantities aggregate at a price. Bids descend; asks ascend.
- Unknown/already-cancelled IDs return `false`. Empty BBO flags are false with zero
  numeric fields. Excess depth returns available levels; nonpositive depth is empty.
- Invalid side/price/nonpositive quantity throws `invalid_argument`; quantity
  overflow throws `overflow_error`. Duplicate live IDs do not mutate the book.
  In C, overflow takes precedence if both conditions apply.
- IDs are `uint64_t`, unique and never reused. Out-of-order IDs, 0 and `UINT64_MAX`
  work. A/C erase cancelled IDs; B retains tombstones, so reuse detection is not a
  common guarantee. Quantities use `long`.

## Designs and complexity


| | A: tree | B: sorted vectors | C: one interleaved array |
|---|---|---|---|
| Order storage | `map<ID, Order>`; live orders only | Sorted `vector<Slot>`; cancelled slots remain marked dead | `unordered_map<ID, Order>`; live orders only |
| Price storage | Two `map<Tick, long>` trees | Two ascending `vector<Level>` arrays | One fixed array: B0,A0,B1,A1,...; descending even bids / ascending odd asks |
| Best price | Last bid/first ask tree node | Last bid/first ask vector element | First two slots, maintained by sorted level shifts |
| Cancel lookup | Tree lookup by ID, then price | Binary search by ID, then price; mark tombstone | Hash ID lookup, then direct tick-to-rank metadata lookup |
| Main cost | Node allocations and pointer traversal | Binary search; shifts on new/deleted levels and arbitrary-ID insertion | Hash allocation/rehash; shifts and rank updates on new/deleted levels |

Let `N` be live orders, `H` all historical adds, `L` occupied levels per side,
`P=5000` and `d=min(requested depth,L)`.

| API | A | B | C |
|---|---|---|---|
| add_order | `O(log N + log L)` | Increasing IDs: amortized `O(log L + L)` on new level, `O(log L)` on existing level; arbitrary IDs: `O(H + log L + L)` worst case | Expected amortized `O(1)` on existing levels, `O(log L+L)` on new levels; hash worst case `O(N+L)` |
| cancel_order | `O(log N + log L)` | `O(log H + log L + L)` if a level is deleted; otherwise `O(log H + log L)` | Expected `O(1)` on partial cancellation, `O(L)` when a level vanishes; hash worst case `O(N+L)` |
| get_bbo | `O(1)` from container extrema | `O(1)` | `O(1)` cached extrema |
| get_bids / get_asks | `O(d)` amortized traversal | `O(d)` contiguous copy | `O(d)` stride-two copy |
| Space | `O(N+L)` node allocations | `O(H+L)` plus vector capacity | `O(N+P)` plus hash buckets |

## Correctness and timing

The [Linux million-event check](results/correctness.txt) compares all three books
against an independent quantity-array reference after every event at full depth.
Edge tests cover empty/one-sided books, aggregation, ordering, depth, cancellation,
invalid inputs, overflow, extreme IDs and dense drains. Physical tests inspect C's
actual array and complete best-pair alignment, including populated relocation.

Seed 42 uses fixed `mt19937` rejection sampling: 700,818 adds, 199,505 live cancels,
99,677 BBO queries, plus 10,000 top-ten bid/ask depth pairs. Every design replays
the same pre-generated stream from an empty book. Cancels uniformly choose a live
ID; IDs increase on add. An empty-book cancel request becomes an add.

Total `steady_clock` replay includes depth/checksum work, excluding generation,
construction, destruction and output. API averages use a separate identical replay
with each call timed. Clock overhead remains included; BBO also stores volatile
results. Its 21–23 ns averages are close to the approximately 19 ns empty intervals.
`steady_clock` is monotonic; C++ does not mandate a clock API or physical resolution.
The checksum/final BBO is printed so measured work is consumed.

Linux replay medians A/B/C are **360.780 / 186.313 / 82.038 ms**.
All 15 timing rows and 135 individual counter rows are consolidated without changing
their values. The [measurement note](results/measurement.md) explains environment,
commands, scopes, hashes and how the medians/ratios are calculated.

## Linux perf

On an authorized Linux host where user-only perf is permitted, repeat for A/B/C:

```sh
perf stat -r 5 -e task-clock:u,cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u,L1-dcache-load-misses:u,dTLB-load-misses:u,page-faults:u ./build/bench --impl A --runs 1 --mode total
perf record -e cpu-clock:u -F 999 --call-graph fp -o perf-A.data -- ./build/bench --impl A --runs 9 --mode total
perf report --stdio -i perf-A.data
```

All nine requested events were available on Nami, with 100% reported running time.
The actual capture used an approved per-child admin profiler and UID/GID 1000
workloads through `setpriv`, without changing security settings. Whole-process
perf includes the helper, generation and teardown; chrono uses the narrower scope
above. Inlining/unresolved libc addresses limit attribution. Fixed A/B/C order,
timing drift and shared-VM scheduling limit generalization. No production p99/max
latency or isolated memory high-water mark is claimed.

## Evidence files

| File | Contents |
|---|---|
| `results/correctness.txt` | Completed million-event full-state/layout check |
| `results/timings.csv` | All five chrono runs for each design |
| `results/perf-stat-{A,B,C}.txt` | Original five-repetition perf stat outputs |
| `results/perf-runs.csv` | Nine event values for each of 15 independent stat runs |
| `results/perf-report.txt` | Original A/B/C self reports and inclusive call graphs |
| `results/measurement.md` | Environment, exact method, provenance and source hashes |

Original per-run files, raw perf recordings, matching ELF and additional Mac
stress/sanitizer logs remain preserved locally; Nami capture files are also intact.
No generated binaries, input datasets or helper scripts are needed in the submission.
