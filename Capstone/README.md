# Low-Latency Order Book

Three implementations of a single-instrument resting order book (add / cancel /
BBO / depth, no matching), plus one benchmark + verification binary.

- **Name / SST email / roll number:** Shreyas S [shreyas.24bcs10401@sst.scaler.com](shreyas.24bcs10401@sst.scaler.com) 24BCS10401

## Build

Requires g++ with C++17. Standard library only.

```bash
scripts/build.sh          # -O2 -DNDEBUG                          -> build/bench       (graded numbers)
scripts/build.sh perf     # -O2 -DNDEBUG -g -fno-omit-frame-pointer -> build/bench_perf  (perf record)
scripts/build.sh debug    # -O0 -g, asserts + invariant checks on -> build/bench_debug
scripts/build.sh asan     # -O1 -g, address + UB sanitizers       -> build/bench_asan  (Linux)
```

Each mode builds the edge tests too (`build/edge_tests[_mode]`, always with asserts on).
Each mode has its own output name, so a debug build can never replace `build/bench`.

Equivalent single command (release):

```bash
g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude -Isrc \
    src/bench.cpp src/workload.cpp src/verify.cpp -o build/bench
```

CMake alternative (Linux): `cmake -S . -B build/cmake && cmake --build build/cmake`.
`CMakeLists.txt` overrides CMake's default Release flags (`-O3`) with `-O2 -DNDEBUG`.
Options: `-DOB_PERF=ON`, `-DOB_SANITIZE=ON`. (Only `scripts/build.sh` has been tested so far.)

## Run

```bash
./build/edge_tests                  # hand-written edge cases (oracle, A, B, C)
./build/bench --verify              # correctness: A, B, C vs a naive oracle, after every op
./build/bench --verify --impl A     # verify a subset
./build/bench --impl A              # benchmark: 5 in-process runs, medians + spread
./build/bench --impl B --runs 1     # one run
./build/bench --generate-only       # build the workload and exit (perf baseline)
```

## Measuring (on the Linux machine)

Run everything below on the machine the report describes; outputs land in `results/`
(see `results/README.md`).

```bash
scripts/build.sh && scripts/build.sh asan
./build/edge_tests_asan && ./build/bench_asan --verify      # sanitizer pass (not timed)
./build/bench --verify > results/verify.txt                  # must end "All implementations produce identical results."
scripts/run_bench.sh                                         # timing: results/bench_*.txt
scripts/run_perf.sh                                          # perf stat + record: results/perf_*
```

The measurements used in `docs/REPORT.md` were produced this way under WSL2 with
`RESULTS_DIR=results/wsl`; see `results/README.md` for what that environment could and
could not measure (no hardware counters).

Check before using any number: the workload hash is `27f23594045a1e72` and the checksum
is identical for A, B and C. Also note the `Clock:` granularity line.

## Profiling

`scripts/run_perf.sh` checks for perf, `perf_event_paranoid` and WSL, then:

- `perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults`
  on `bench --impl X --runs 1 --total-only`, one total pass per process, so the counters
  don't include ~2M extra clock calls from the per-op pass. `perf stat -r 5` is the
  repetition here (5 processes), not `bench --runs`.
- The same counters on `bench --generate-only` as a **baseline**. Every process also
  generates the workload, so compare A/B/C against the baseline before attributing page
  faults or instructions to a book.
- `perf record --call-graph fp` on `build/bench_perf` (same `-O2`, plus frame pointers),
  then `perf report` hot spots (flat and with callers) to `results/perf_report_*.txt`.
- `scripts/perf_summary.sh` turns the raw files into `results/perf_summary.md`, with IPC =
  instructions / cycles, branch-miss % = branch-misses / branches × 100, and cache-miss % =
  cache-misses / cache-references × 100. Unsupported counters are shown as `n/s` and listed
  in `results/perf_unsupported.txt`, never estimated.

## Interface changes

- **Public API unchanged.** Prices stay `double` at the API boundary, as specified.
- **Internally, prices are integer ticks** (`Tick = int32_t`, 1 tick = 0.01),
  converted once with `std::llround(price * 100)` (see `include/types.h`). Floating-point
  map keys and comparisons are unsafe (`100.01 * 100` may be `10000.999...`).
- **Preconditions** (asserted in debug builds, not checked in release builds):
  `add_order` IDs are new, prices lie in the side's band (bids 50.00–99.99, asks
  100.01–150.00; 100.00 is never valid), `1 <= qty <= INT32_MAX` (the upper bound comes
  from C packing qty into 32 bits); `depth >= 0`.
- No virtual interface: the harness is templated on the book type, so calls in the
  timed loop are direct.

## Verification

`--verify` replays two workloads through each implementation in lockstep with a
deliberately naive oracle (`src/oracle.h`). After every op it compares the BBO,
`get_bids(10)`, `get_asks(10)` and every cancel result. Periodically it compares full
depth and runs the book's `check_invariants()`. The first mismatch prints the op
index, the op, and expected vs actual, and the exit code is non-zero.

1. **graded**: the benchmark workload (1M ops, seed 42, 70/20/10).
2. **thin-book**: verification only, 200k ops, seed 43, 45/45/10, scrambled 64-bit ids.
   The graded workload fills every price level early and almost never empties one again,
   and its sequential ids rarely collide in a hash table. This second workload exercises
   the empty-level, best-price-change and hash-collision paths (see `docs/DECISIONS.md`).

C is verified twice: with its default 2M-slot hash table, and starting from a 16-slot
table so that growth and long probe chains are exercised.

## Benchmark methodology

```bash
scripts/run_bench.sh                      # A, B, C x 5 fresh processes -> results/bench_*.txt
./build/bench --impl C                    # 5 runs in one process, medians + spread
./build/bench --impl C --runs 1 --total-only   # one total-time pass only (for perf)
./build/bench --impl C --latency          # adds p50/p99/p99.9/max per op type
```

- **Workload:** 1M ops generated up front from `std::mt19937(42)` (70% add, 20% cancel,
  10% BBO; sequential ids), before any timing. The op-list hash (`27f23594045a1e72` with
  libstdc++) is printed so every run can be checked against the same stream.
- **Depth queries:** `get_bids(10)` + `get_asks(10)` after every 100th op (10,000 pairs) are
  **extra** to the 1M ops. They are included in the total time and reported as their own
  category (`ns/depth` is the cost of one bids+asks pair).
- **Two passes per run**, each on a freshly constructed book (construction and destruction
  are not timed):
  1. *Total pass*: one `steady_clock` pair around the whole loop, so clock overhead is
     negligible. This gives **total time**.
  2. *Per-op pass*: a clock pair around every op. **ns/op = mean measured duration − mean cost
     of an empty clock pair**. The empty-pair cost is calibrated at startup (median of 5 ×
     1M pairs) and printed together with the clock's **granularity**. Per-op means for very
     cheap ops (BBO) are close to the noise floor; read them next to the printed overhead.
- **Repetition:** report the median. In-process runs share heap state, so `bench` prints the
  spread and warns above 1.5×. `scripts/run_bench.sh` runs each impl in **5 separate
  processes** (`--runs 1`) and takes medians across processes. That is the recommended
  source for the results table.
- **Dead-code elimination:** a checksum folds in every BBO, depth result and cancel result.
  It must match across runs, passes and implementations (the program exits 1 otherwise),
  and the final BBO is printed.
- **Latency percentiles** (`--latency`, bonus): raw per-op durations from the last run,
  clock overhead **not** subtracted. Only meaningful if the clock granularity is a few ns.
  On Windows/MinGW `steady_clock` ticks in 100 ns steps, so percentiles there are not usable.
- **Build check:** `bench` prints the compiler, whether it was optimised, and whether NDEBUG
  is set, and warns if the binary is not a valid timing build.
- **Caveat on per-op numbers:** the per-op pass puts a clock read around every op, and that
  measurably slows C (not A or B) beyond the cost of the reads. See `--batch` below and the
  report's data-quality notes. Treat total time as the reliable number for C.

### Extra experiments (not graded)

```bash
./build/bench --impl C --batch                # block-timed fit + clock-perturbed total pass
./build/bench --impl C --ids scrambled        # same book evolution, scrambled 64-bit ids
./build/bench --impl C --mix thin             # 45/45/10 thin-book workload
TAG=scrambled BENCH_ARGS="--ids scrambled" scripts/run_bench.sh   # 5 processes per impl
```

- `--batch` replaces the per-op pass with two things. (1) It times 50-op blocks and fits
  per-type costs by least squares; R² is printed, and a low R² means the fit is noise. Under
  WSL R² was ≈ 0, so this fit is not used. (2) A *clock-perturbed* total pass: the total-pass
  loop plus one clock read after every op. It reports how much slower that loop is beyond the
  reads' own cost.
- `--mix` / `--ids` select non-graded workloads. `bench` prints a NON-GRADED warning for any
  workload other than the graded one; report those numbers separately.

## Report

`docs/REPORT.md`: the 1.5-page report (results table and answers to 11.1–11.6).
`docs/REPORT_DETAILS.md`: the supporting evidence behind it (data-quality checks, profile hot
spots, substitute experiments, workload facts, and a claim → evidence table). Every number in
both comes from a file in `results/`.

## Implementations

The three designs differ in all four required aspects. The full rationale, rejected
alternatives and caveats on each Big-O entry are in `docs/DECISIONS.md`.

|       | Orders                          | Levels                             | Best price                     | Cancel lookup               |
|-------|---------------------------------|------------------------------------|--------------------------------|-----------------------------|
| **A** | `std::unordered_map`            | `std::map` per side                | `begin()`                      | hash lookup → tree find     |
| **B** | `std::map<id>`                  | sorted `std::vector`, best at back | `back()`                       | tree lookup → binary search |
| **C** | open-addressing hash, pre-sized | flat 10,001-slot array             | cached tick + two-level bitmap | hash probe → array index    |

Notation: N = live orders, L = levels on a side (≤ 5,000), d = min(depth, levels).

### A: tree levels + chained-hash orders (`include/book_a.h`)

Orders are separately allocated hash-table nodes (`id → {side, tick, qty}`). Each side's
levels are a red-black tree keyed by tick (bids ordered by `std::greater`), holding
`{qty, count}`. The best price is the tree's leftmost node. Cancel finds the order by hash,
then its level by tree lookup, subtracts the qty, and erases the level when its count reaches 0.

| Operation           | Complexity                                              |
|---------------------|---------------------------------------------------------|
| add_order           | O(log L) + O(1) average (hash insert; O(N) on a rehash) |
| cancel_order        | O(1) average + O(log L)                                 |
| get_bbo             | O(1)                                                    |
| get_bids / get_asks | O(d)                                                    |

### B: sorted-vector levels + tree orders (`include/book_b.h`)

Orders are nodes in a red-black tree keyed by id. Each side's levels are a contiguous
sorted vector of `{tick, qty, count}` ordered worst → best, so the best level is `back()`.
Cancel finds the order by tree lookup, then its level by binary search, and erases the level
if it empties (shifting every better level).

| Operation           | Complexity                                                     |
|---------------------|----------------------------------------------------------------|
| add_order           | O(log N) + O(log L), + O(L) shift when the tick is a new level |
| cancel_order        | O(log N) + O(log L), + O(L) shift when the level empties       |
| get_bbo             | O(1)                                                           |
| get_bids / get_asks | O(d)                                                           |

### C: direct-indexed levels + bitmap + open-addressing orders (`include/book_c.h`)

Orders are 16-byte slots in a pre-sized open-addressing table (linear probing, Fibonacci
hashing, backward-shift deletion; doubles above load 0.5). Levels are one array slot per
tick (`index = tick − 5000`). The best bid and ask indexes are cached. When the best level
empties, a two-level bitmap (157 words + 3 summary words) finds the next one with clz/ctz.
Cancel probes the table, indexes the array directly, and updates the bitmap and cached best
if needed.

| Operation           | Complexity                                                       |
|---------------------|------------------------------------------------------------------|
| add_order           | O(1) expected (O(N) once per table doubling)                     |
| cancel_order        | O(1) expected (bitmap search ≤ 5 words for the fixed tick range) |
| get_bbo             | O(1)                                                             |
| get_bids / get_asks | O(d)                                                             |
