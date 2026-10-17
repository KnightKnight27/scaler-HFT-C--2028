# Resting order book — Rushi Jadhav

**SST email:** rushi.24bcs10144@sst.scaler.com
**Roll number:** 10144
**Team:** individual submission

Three C++17, single-threaded, standard-library implementations for one instrument.
Orders rest until cancelled. There is no matching or trade generation. The supplied
assignment PDF takes precedence over the informal `../capstone.txt` lecture notes.
The PDF deadline is 17 October 2026 at 11:59 PM, with no timezone specified.

From this directory, with CMake >=3.16, a C++17 compiler and Python 3:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
./build/bench --help
./build/bench --verify
./build/bench --verify --seed 123456 --ops 100000
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
./scripts/check.sh
python3 scripts/measure.py --cpu 0
python3 scripts/collect_perf.py --cpu 0
python3 scripts/tables.py
```

`check.sh` runs Release, Debug and optimized (-O2) ASan/UBSan validation,
stopping on failure. Sanitizer checks retain the same coverage and run-time
assertions; they are separate from the uninstrumented measurement build.
CTest provides the bounded suite via `ctest --test-dir build --output-on-failure`.
No build or binary is committed. Release explicitly uses **-O2 -DNDEBUG**, with
`-g -fno-omit-frame-pointer`, not the CMake default -O3. Actual compile commands
and source/binary SHA-256 identities are in the measurement metadata. No native
CPU optimization flags are used. ASan/UBSan numbers are never used as timings.

## Public API and contracts

Shared [types](include/types.hpp) replace the PDF's `double` prices with **integer
cent ticks** (`int`): 5000 means 50.00, 9999 means 99.99; asks are 10001..15000.
Quantity is `int64_t`, IDs are arbitrary `uint64_t` including 0 and UINT64_MAX.
`Side::Buy` and `Side::Sell` are the only valid sides.

Each of `ob::BookA`, `ob::BookB`, `ob::BookC` provides:

```cpp
void add_order(uint64_t id, Side side, int price_cents, int64_t qty);
bool cancel_order(uint64_t id);
BBO get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

External decimal inputs must be converted to cents with an explicit exact-grid
check before calling this API; floating-point price overloads are deleted, so accidental
double/float calls fail to compile instead of truncating. For example, decimal string `"100.01"` maps exactly to 10001; a value
like `"100.015"` must be rejected by the caller. The tick API cannot represent a
fractional cent. Bands exclude 100.00. Invalid side, out-of-band price, qty <=0,
or duplicate **live** ID throws `std::invalid_argument`. Aggregate overflow above
INT64_MAX throws `std::overflow_error`. Rejected additions leave state unchanged.
A single order and its level aggregate may be as large as INT64_MAX. There is no
cross-level total stored that could overflow. Allocation failure propagates with
logical state preserved.

IDs must never be reused by the caller, including after cancellation. Historical
IDs are deliberately not stored or checked. Unknown/already-cancelled cancellation
returns false. Nonpositive depth returns an empty vector; excessive depth returns
only occupied levels. Bids descend and asks ascend. An absent BBO side has false
presence and zero price/quantity, independently of the other side. Queries are
const and do not change logical contents.

## Designs and complexity

The [pre-implementation design](docs/DESIGN.md) records the choices.

| Design | Order storage and cancel lookup | Levels and BBO |
|---|---|---|
| [A](include/book_a.hpp) | hashed ID → complete Order | two ascending price trees, reverse bid/forward ask endpoints |
| [B](include/book_b.hpp) | ordered ID tree → complete Order | ascending vectors; binary search and insert/erase shifts; endpoint BBO |
| [C](include/book_c.hpp) | packed Order vector + hashed ID → slot; swap-remove repairs moved slot | fixed quantity arrays + occupancy words + cached best indices |

Let N be current live orders, M the peak live orders, L occupied levels across
both sides, P=10,000 valid ticks, W=ceil(5000/64)=79 words per side,
K=min(max(D,0), occupied levels on the queried side). Space excludes the returned
vector of K levels. Hash lookups are expected O(1), worst O(N); hash rehashing and
vector growth are amortized, never worst-case constant.

| Operation | A | B | C |
|---|---|---|---|
| add | expected amortized O(log(L+1)), worst O(N+log(L+1)) | O(log(N+1)+log(L+1)+L), shifts/growth | expected amortized O(1), worst O(N) from hashing/growth |
| cancel | expected O(log(L+1)), worst O(N+log(L+1)) | O(log(N+1)+log(L+1)+L), shifts if level empties | expected O(W+64), worst O(N+W+64) |
| BBO | O(1) | O(1) | O(1) |
| bids/asks | O(K) | O(K) | O(W+64K), including portable bit-position loops |
| retained space | O(N+L+M) including hash buckets | O(N+P), vector capacity may outlive occupied levels | O(M+P), packed/hash capacities retain high-water marks |

C only scans occupancy words when the last order at the cached best tick is
cancelled. Price array indexing and bit updates are constant; scanning is bounded
by the domain, not asymptotically O(1) if P varies. Depth iterates occupied words
and bits rather than every price. Its portable bit-position search performs at
most 63 shifts per returned level. There are no stale order entries, lazy deletion,
or custom hash tombstones. Vectors/hash buckets retain capacity after deletion;
none of the designs retains an ever-growing history of cancelled IDs. Node
allocation and C's packed-vector growth can still produce latency spikes.

## Verification coverage

[Verifier](src/verify.cpp) checks deterministic API cases, invalid inputs, overflow,
ID extremes, partial aggregation, best/final removal, and query boundaries. It
fills **all 10,000 ticks** and drains in a coprime permutation, comparing BBO and
full depth and independently rebuilding reference sums after every operation.
Three bounded random seeds (0, 7, 2026; 2,000 operations each) use sparse IDs near
UINT64_MAX with the same full checks. These exercise growth, rehash and C's slot
repair. There are no hash-table tombstones to test.

The default `--verify` then executes **1,000,000 operations, seed 42**, comparing
cancel outcomes, both BBO flags/prices/quantities and both top-10 results after
**every primary operation**. Every 100 operations checks both depth APIs again;
full depth and a reference ledger-to-aggregate invariant are checked every
10,000 operations and at the final state. Full depth is not checked after every
million-run operation. The independent reference uses an ordered ID ledger and
one combined-price map; ledger reconstruction uses a separate fixed sum array.
Mismatches include seed/step or sweep index, operation context and expected/actual
data, and return nonzero. Tests use checks that remain active with NDEBUG.

## Measurement method

[Workload generator](src/workload.hpp) uses mt19937_64 and an explicitly specified
unbiased rejection sampler. A separate vector of live IDs supports uniform
selection and swap-removal for scheduled cancels. All trace generation is outside
timing. Empty-state cancel draws become adds and are counted. IDs grow from zero;
random correctness suites also exercise nonsequential full-width IDs. The trace
hash serializes logical values explicitly (FNV-1a), never struct padding.

Official defaults are 1,000,000 primary operations, seed 42, approximately
70% adds / 20% live cancels / 10% BBO queries; adds use uniform side, valid tick,
and qty 1..100. Both top-10 depth queries occur at each 100-operation boundary,
including 1,000,000 (20,000 additional calls). Output reports actual counts,
trace hash, successful cancels, query checksum and final BBO in cents.

`steady_clock` measures each operation separately and the whole loop. Per-call
intervals include dispatch but exclude checksum folding and duration accumulation;
total includes clocks, loop overhead, checksum consumption and mandatory depth
queries. Book construction/destruction, generation and all formatting/I/O are
outside internal timing. A single default invocation performs **one replay**.
`--warmup` performs an identical untabulated replay on a separate book before the
measured replay. No book is preallocated using future trace information. All
books start with default container capacity, also during warmup.

`measure.py` uses one CPU (0 here), rotates ABC/BCA/CAB/ABC/BCA, runs sequentially,
and saves each of five runs plus independently calculated medians for each
metric. It checks trace/checksum/count agreement and every cancellation's success.
No competing test or build runs during official measurements. No governors or
security settings are modified. The supplementary `--mode bulk` disables
per-call clocks but keeps checksum/depth work; never compare its per-call fields
(which are zero/not measured) with instrumented results.

## Evidence and interpretation

- [Results table](results/table.md), [15 raw timed runs](results/instrumented-runs.csv),
  [medians](results/instrumented-medians.json), [provenance](results/instrumented-metadata.json).
- [Passing Release](results/correctness-release.txt), [extra seed](results/correctness-extra-seed.txt),
  [Debug](results/correctness-debug.txt), [sanitizers](results/correctness-sanitizers.txt).
- [Environment](results/environment.txt), [perf command status](results/perf/commands.json),
  per-implementation raw `.stdout.txt` / `.stderr.txt` and readable reports under
  [results/perf](results/perf).
- [Two-page performance report](docs/report.pdf), [report source](docs/report.md),
  [viva guide](docs/VIVA.md), [acceptance matrix](docs/ACCEPTANCE.md),
  [final review and fresh-build verification](docs/FINAL_VERIFICATION.md).

Linux perf is needed only for profiling, and CPU affinity must name an allowed
CPU. This hybrid Intel CPU exposes distinct core/atom PMUs; pinned CPU 0 uses the
core PMU. External perf covers process startup, workload generation, destruction,
and internal clock/checksum work as well as the replay. It is **not book-only**
accounting. Perf repetition means/stddev are distinct from the five-run benchmark
medians. IPC uses comparable instructions/cycles; branch miss % uses comparable
branch-misses/branches. Multiplexing, unavailable counters and profiling limits
are recorded in the table/report; unavailable is N/A, never zero. Page faults
measure fault events, not total memory usage. Average latencies do not establish
tail guarantees. Report generation optionally uses Python ReportLab; it is not a
runtime/build dependency. See `scripts/render_report.py`.

To repeat the exact required profiler command on another Linux machine:

```sh
perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./build/bench --impl A
perf record -g -o results/perf-A.data -- ./build/bench --impl A
perf report --stdio -i results/perf-A.data
# Repeat for B and C. Machine-specific .data files are ignored by Git.
```

Use `collect_perf.py` for dated commands, preserved exit codes, split fallbacks
and all three implementations. It exits nonzero if any requested command failed,
including successful fallback cases; inspect `commands.json` to distinguish a
capability limitation from a program failure. Reproduction scripts never hide
failures through pipelines.

The local run used an extracted `perf` package because perf was initially absent.
In this existing workspace, reproduce with the same local executable:

```sh
task_perf_root="$(cd ../../.. && pwd)/.local-tools/perf"
LD_LIBRARY_PATH="$task_perf_root/usr/lib" python3 scripts/collect_perf.py --cpu 0 --perf "$task_perf_root/usr/bin/perf"
```

On a fresh clone, install/provide Linux perf as an environment prerequisite instead.
The version and local package setup are recorded in `results/environment.txt`.
To reproduce the supplementary instrumentation investigation after required runs:

```sh
python3 scripts/measure.py --cpu 0 --mode bulk
python3 scripts/profile_bulk.py --cpu 0  # use --perf /path/to/perf if needed
```

[Bulk runs](results/bulk-runs.csv) and [bulk medians](results/bulk-medians.json)
are separate from the main results. Original and bulk flat self-sample reports
and call graphs are retained in `results/perf/`. All active performance-core
counters were available. Unused atom PMU entries were not counted, and its L1
event was unsupported; only core counters enter calculations. Core event output
shows no reduced running percentage/scaling indicator. Some outer call-chain
startup frames are unresolved, so analysis uses resolved symbols and flat self
reports. No samples were lost. Local install's missing optional `tips.txt` affects
only perf's help-tip footer, not measurement or symbol reports.
