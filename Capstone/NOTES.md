# Architectural Decisions & Notes

This document outlines the key design decisions, trade-offs, and constraints considered while building the three order book implementations.

## D1: Integer Ticks instead of Double Prices
**Decision:** Prices are converted to and stored internally as `int32_t` ticks (`price * 100`). The conversion happens once at the API boundary using `std::llround()`.

**Context:** The assignment API accepts `double` for prices. However, floating-point math is imprecise. For example, `0.29 * 100` evaluates to `28.999999999999996`. If a truncating cast (`static_cast<int>`) is used, the order silently lands at tick 28 instead of 29. Furthermore, comparing doubles for equality in `--verify` can produce false mismatches due to minuscule representation differences.

**Consequence:** Internal map keys, array indexes, and comparisons are purely integer-based. This guarantees exactness, makes `Level::operator==` reliable, and allows Implementation C to use direct array indexing safely.

## D2: Decoupled Orders and Price Levels
**Decision:** Order details are stored separately from price level aggregates. 
*   **Order Storage:** ID -> {price, qty, side}
*   **Level Storage:** Price -> Total Qty

**Context:** The spec explicitly states there is no order matching. We only need to know the *total* quantity resting at a price level. Storing a linked list or `std::vector` of individual orders at every price level (like a full matching engine would) is unnecessary overhead.

**Consequence:** `cancel_order(id)` must perform two steps: look up the order by ID to find its price/qty, and then subtract that qty from the level aggregate. If the aggregate drops to zero, the level is removed. 

## D3: Implementation A (`std::map`) - The Baseline
**Decision:** `std::map` for levels, `std::unordered_map` for orders. Bids use `std::greater<Tick>` so the highest price is at `begin()`.

**Context:** This serves as the reference "oracle" implementation. 
**Trade-offs:** 
*   **Pros:** `std::map` automatically keeps prices sorted. Finding the BBO is O(1) via `begin()`.
*   **Cons:** Every insert allocates a new tree node (heap `malloc`). Traversing the tree causes pointer-chasing and constant cache misses. `std::map` insertions/deletions are O(log L).

## D4: Implementation B (Sorted Vector) - Cache Locality
**Decision:** A sorted `std::vector<Level>` for levels, ordered so the *best* price is at the `back()`.

**Context:** Vectors offer contiguous memory, which is highly friendly to the CPU prefetcher compared to `std::map`'s scattered nodes.
**Trade-offs:**
*   **Pros:** Great spatial locality. Finding the BBO is O(1) via `back()`.
*   **Cons:** Inserting or erasing in the middle of a vector requires shifting all elements after it (`memmove`). 
*   **Why best-at-back?** In real HFT, the most active price levels are at the top of the book. By putting the best price at the back, we minimize the number of elements that need to be shifted during additions and cancellations.

## D5: Implementation C (Flat Array) - The Production Choice
**Decision:** A pre-allocated flat array `std::array<Qty, 10001>` indexed by `tick - MIN_TICK`. Best bid/ask are cached in variables. Orders are stored in a `std::vector` indexed by ID.

**Context:** This design optimizes for the absolute fastest hot path with zero heap allocations.
**Trade-offs:**
*   **Pros:** O(1) add, cancel and BBO. The level array is fixed (about 80 KB) and contiguous. No hashing or tree walks.
*   **Cons:** `ord_` grows with `resize(id + 1)`, so it does allocate and copy on the hot path when it reallocates (visible as page faults, see REPORT 3.4), and its size follows the largest id rather than the number of live orders. When the best level empties, a linear rescan finds the next one.
*   **Future:** reserve or pool the order storage; replace the rescan with a bitmap of non-empty ticks.

## D6: Benchmarking Methodology
**Decision:** Pre-generate 1,000,000 operations into a `std::vector<Op>` before timing. Use C++ templates for the interface instead of virtual functions.

**Context:** 
*   If we generate random numbers inside the timing loop, we measure the RNG, not the order book.
*   Virtual functions add an indirect jump (vtable lookup) on every operation. Templates allow the compiler to inline `add_order` and `cancel_order` directly into the benchmark loop.

**Consequence:** The benchmark is identical for A, B, and C, completely deterministic (seed 42), and measures only the exact data-structure operations.