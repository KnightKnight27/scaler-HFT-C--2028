# Capstone — Low-Latency Order Book

**Name:** Om Malviya
**SST email:** om.24bcs10448@sst.scaler.com
**Roll number:** 2024bcs10448

A single-instrument **resting** order book (no matching, no trades) implemented
**three genuinely different ways**, with one shared benchmark that proves all three
agree on an identical workload and measures their performance.

- **A — ordered `std::map`** (balanced BST)
- **B — `unordered_map` levels + cached best** (hash, lazy best recompute)
- **C — dense price arrays + bitmap index** (my own design)

---

## Build

Single command (as allowed by the spec):

```bash
g++ -O2 -std=c++17 bench.cpp -o bench
```

Or with CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build          # produces ./build/bench
```

Requires a C++17 compiler. Standard library only — no Boost / external libraries.
Developed with GCC 15.1.0 on macOS (arm64).

## Run the correctness test

```bash
./bench --verify
```

Replays the same random workload through A, B and C and compares the result of
**every** operation (BBO after each op; `get_bids(10)`/`get_asks(10)` at every
100-op checkpoint), and checks the required edge cases (cancel-unknown → `false`,
empty-book BBO, insufficient depth). Output:

```
Running correctness test...

Implementation A: PASS
Implementation B: PASS
Implementation C: PASS

All implementations produce identical results.
```

## Run the benchmarks

One program, implementation selected on the command line (same workload for all):

```bash
./bench --impl A
./bench --impl B
./bench --impl C
```

Each runs **1,000,000 operations** (~70% add, 20% cancel, 10% BBO; plus
`get_bids(10)`+`get_asks(10)` every 100 ops) **5 times and reports the median**,
then prints the final BBO and a checksum so `-O2` cannot optimize the work away.
Optional flags: `--ops N` (default 1,000,000), `--runs R` (default 5).

## Profiling

- **Linux:** `bash perf.sh` — runs `perf stat -r 5` (cycles, instructions,
  branches, branch-misses, cache-misses, L1-dcache-load-misses, dTLB-load-misses,
  page-faults) and `perf record -g` for A, B and C.
- **macOS (this machine):** Linux `perf` is **not available** on macOS/arm64, so
  `bash macos_profile.sh` uses `/usr/bin/time -l` to report instructions, cycles
  (→ IPC), peak memory, page faults and context switches — the available local
  substitute. See `REPORT.md` for the measured numbers and which counters needed
  Linux `perf`.

---

## Interface notes

The required API is implemented exactly:

```cpp
void add_order(uint64_t id, Side side, double price, long qty);
bool cancel_order(uint64_t id);
BBO  get_bbo() const;
std::vector<Level> get_bids(int depth = 10) const;
std::vector<Level> get_asks(int depth = 10) const;
```

- **Integer ticks internally.** The public API keeps `double price`, but every
  implementation converts to an integer tick at the boundary (`tick =
  llround(price*100)`) and works in ticks, because the tick size is fixed at 0.01
  (Hint 3). Bid ticks 5000–9999, ask ticks 10001–15000 (10,000 levels total).
- **Implementation C assumes dense, monotonically increasing order ids** (the
  benchmark assigns 1, 2, 3, …). It stores orders in a `std::vector<Order>`
  indexed directly by id, which is what makes its cancellation an O(1) array
  lookup. A and B use a hash map keyed by id and do not need this assumption.

## Data-structure choices

| | **A — std::map** | **B — unordered_map** | **C — array + bitmap** |
|---|---|---|---|
| Store orders | `unordered_map<id,rec>` | `unordered_map<id,rec>` | `vector<Order>` indexed by id |
| Store price levels | ordered `std::map<tick,qty>` | `unordered_map<tick,qty>` | flat `vector<long>` per side |
| Find best price | `map.begin()` | cached best; rescan on empty | highest/lowest set bit (bitmap) |
| Locate order to cancel | hash lookup | hash lookup | direct array index by id |

## Big-O complexity (L = number of active price levels)

| Operation | A (std::map) | B (unordered_map) | C (array + bitmap) |
|---|---|---|---|
| `add_order` | O(log L) | O(1) avg | O(1) |
| `cancel_order` | O(log L) | O(1) avg, **O(L)** when the best level empties | O(1) |
| `get_bbo` | O(1) | O(1) | O(1) (≤79-word bit scan) |
| `get_bids` / `get_asks` | O(depth) | **O(L + depth·log depth)** | O(depth) |

## Files

| File | Purpose |
|---|---|
| `common.h` | `Side`, `Level`, `BBO`, tick↔price helpers |
| `impl_a_map.h` | Implementation A (ordered `std::map`) |
| `impl_b_hashmap.h` | Implementation B (`unordered_map` + cached best) |
| `impl_c_array.h` | Implementation C (dense arrays + bitmap) |
| `bench.cpp` | workload, `--verify`, `--impl A/B/C`, timing, checksum |
| `CMakeLists.txt` | CMake build (`-O2`, C++17) |
| `perf.sh` | Linux `perf stat` / `perf record` driver |
| `macos_profile.sh` | macOS `/usr/bin/time -l` profiling substitute |
| `REPORT.md` | results table + performance analysis |
