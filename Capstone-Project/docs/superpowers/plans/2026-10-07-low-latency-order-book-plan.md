# Low-Latency Order Book Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Implement three correct, measurably different C++17 order books plus shared verification, benchmarking, profiling instructions, and report templates.

**Architecture:** A shared tick-based interface isolates the benchmark from three implementations: ordered maps, lazy heaps, and fixed price-indexed arrays. One deterministic workload drives verification and benchmarking so differences come from the data structures rather than input variation.

**Tech Stack:** C++17, standard library only, CMake, `g++ -O2`, `std::chrono::steady_clock`, Linux `perf` when available.

**Spec:** `docs/superpowers/specs/2026-10-07-low-latency-order-book-design.md`

## Global Constraints

- One instrument and one thread only.
- Valid bid ticks are `5000..9999`; valid ask ticks are `10001..15000`; tick `10000` is the mid-price and cannot contain orders.
- Order IDs are unique and never reused.
- No matching, trade generation, Boost, or external order-book library.
- The benchmark uses 1,000,000 operations and seed `42`.
- Every 100 operations, call both depth queries.
- Run each implementation five times and report medians.
- Build with C++17 and `-O2`.

## Review Focus

- Invalid side/price/quantity input must be rejected consistently; test boundary ticks and the forbidden mid tick in `tests/order_book_contract.cpp`.
- Cancelling an unknown or already-cancelled ID must return `false` without changing book state; test in the common contract suite.
- Empty books and insufficient depth must return the required empty/short results; test in the common contract suite.
- Heap stale entries must never change observable levels or BBO; test add-cancel-readd sequences in the heap-specific suite.
- All implementations must agree after the same long mixed workload; test through `--verify`.

---

### Task 1: Build configuration and shared model

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/order_book.hpp`
- Create: `include/price.hpp`
- Create: `tests/order_book_contract.cpp`

**Interfaces:**
- Produce `Side`, `Tick`, `OrderId`, `Level`, `BBO`, and abstract `OrderBook` declarations from the spec.
- Produce tick validation and decimal display helpers in `price.hpp`.

- [ ] **Step 1: Write the failing contract tests** for empty BBO, add/query, depth truncation, cancel success, cancel unknown, and invalid input.
- [ ] **Step 2: Run `cmake -S . -B build && cmake --build build`** and confirm the test target fails because implementations are not registered yet.
- [ ] **Step 3: Add the shared declarations and CMake targets** with C++17 and `-O2`.
- [ ] **Step 4: Run the contract test target** and confirm the test harness builds; implementation-specific tests remain disabled until Tasks 2–4.

### Task 2: Implementation A — ordered maps

**Files:**
- Create: `src/map_order_book.hpp`
- Create: `tests/map_order_book.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Implement `OrderBook` using `std::map<Tick, long>` for side levels and `std::unordered_map<OrderId, Order>` for live order metadata.

- [ ] **Step 1: Add tests** covering aggregation at one price, highest bid/lowest ask, descending/ascending depth order, and cancellation removing empty levels.
- [ ] **Step 2: Run `cmake --build build && ctest --test-dir build`** and confirm the new tests fail before implementation.
- [ ] **Step 3: Implement the minimal map-backed book** with validation, ID lookup, level aggregation, and required queries.
- [ ] **Step 4: Run the focused tests and contract tests**; expect all to pass.

### Task 3: Implementation B — lazy heaps

**Files:**
- Create: `src/heap_order_book.hpp`
- Create: `tests/heap_order_book.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Implement `OrderBook` using authoritative side maps, a max-heap for bids, a min-heap for asks, and the shared live-order lookup.

- [ ] **Step 1: Add tests** for stale heap entries after cancellation, re-adding a previously emptied level, best-price cleanup, and depth cleanup.
- [ ] **Step 2: Run the focused tests** and confirm they fail before implementation.
- [ ] **Step 3: Implement lazy stale-entry cleanup** so every heap result is checked against the authoritative current level quantity.
- [ ] **Step 4: Run heap, contract, and map tests**; expect all to pass.

### Task 4: Implementation C — fixed price-indexed arrays

**Files:**
- Create: `src/array_order_book.hpp`
- Create: `tests/array_order_book.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Implement `OrderBook` with 5,000-entry bid and ask arrays, live-order hash lookup, and maintained best ticks.

- [ ] **Step 1: Add tests** for both price bands, best-price movement after cancelling the best level, empty-level skipping, aggregation, and depth ordering.
- [ ] **Step 2: Run the focused tests** and confirm they fail before implementation.
- [ ] **Step 3: Implement offset indexing** (`tick - 5000` for bids and `tick - 10001` for asks) with bounded best-price recovery.
- [ ] **Step 4: Run all three implementation suites and the common contract suite**; expect all to pass.

### Task 5: Deterministic workload and cross-implementation verification

**Files:**
- Create: `bench/workload.hpp`
- Create: `tests/cross_implementation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produce one workload generator using `std::mt19937(42)` and one runner that applies every operation to all three `OrderBook` implementations.

- [ ] **Step 1: Add a small deterministic workload test** asserting stable operation count, valid live-order cancellation selection, and repeatable operation sequence.
- [ ] **Step 2: Run the test and confirm it fails before the generator exists.**
- [ ] **Step 3: Implement the workload generator** with approximately 70% add, 20% live cancel, 10% BBO, and depth calls every 100 operations.
- [ ] **Step 4: Compare BBO, bids, asks, prices, and quantities after every observable operation.**
- [ ] **Step 5: Run the cross-implementation test** and expect A/B/C agreement.

### Task 6: Benchmark executable

**Files:**
- Create: `bench/benchmark.hpp`
- Create: `bench/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Support `./bench --verify`, `./bench --impl A`, `./bench --impl B`, and `./bench --impl C`.
- Report total milliseconds, average nanoseconds for add/cancel/BBO, five-run median, final BBO, and checksum.

- [ ] **Step 1: Add CLI and timing tests** for implementation selection, invalid selector handling, and checksum stability on a small workload.
- [ ] **Step 2: Run the tests and confirm they fail before the executable exists.**
- [ ] **Step 3: Implement the shared benchmark runner** using `std::chrono::steady_clock` and a factory that selects only the requested implementation.
- [ ] **Step 4: Build with `cmake --build build --config Release`** and run all four commands.
- [ ] **Step 5: Confirm identical workload configuration and valid final checksums** across A, B, and C.

### Task 7: Documentation and profiling report templates

**Files:**
- Create: `README.md`
- Create: `reports/benchmark-results.md`
- Create: `reports/performance-analysis.md`
- Create: `reports/correctness-results.md`

**Interfaces:**
- Document build/run commands, tick-interface change, each design, complexity table, correctness output, benchmark output, and `perf` commands.

- [ ] **Step 1: Document the exact build and verification commands** and the three data structures.
- [ ] **Step 2: Add result tables** for total time, operation latency, IPC, branch misses, cache misses, L1 misses, and page faults.
- [ ] **Step 3: Add the required analysis prompts** covering fastest operations, branches, cache behavior, page faults, production choice, and next experiments.
- [ ] **Step 4: Run the documented commands** and replace correctness placeholders with actual output; leave performance values blank until measured on the target Linux machine.

### Task 8: Final verification and local measurement

**Files:**
- Modify: `reports/benchmark-results.md`
- Modify: `reports/correctness-results.md`
- Modify: `reports/performance-analysis.md`

- [ ] **Step 1: Run the complete build and test suite** with `cmake --build build && ctest --test-dir build --output-on-failure`.
- [ ] **Step 2: Run `./build/bench --verify`** and capture the pass output.
- [ ] **Step 3: Run each implementation five times** and record medians in the benchmark table.
- [ ] **Step 4: Run `perf stat -r 5`** with available assignment counters for A, B, and C; record unavailable counters explicitly.
- [ ] **Step 5: Run `perf record -g` and `perf report`** for each implementation and use the observed hot paths in the analysis.
- [ ] **Step 6: Re-run the full suite after documentation changes** and confirm no source or test regression.
