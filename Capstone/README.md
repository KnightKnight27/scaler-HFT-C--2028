# Low-Latency Limit Order Book

Name : Sahadheep
Email : challa.24bcs10276@sst.scaler.com
rollno : 10276

A single-instrument resting limit order book implemented in C++17 using the standard library only. The project uses `-O2`, runs single-threaded, and implements three approaches that are differential-tested and benchmarked.

## Implementations

| Implementation | Data Structure |
| A              | `std::map` (red-black tree) |
| B              | Max/min `std::priority_queue` with generation-tracked lazy deletion         |
| C              | Fixed-size price-indexed arrays (custom design) |
| D              | Segment tree over flat price arrays |

## Rules and Design Decisions

- Tick size: `0.01`
- Bid price range: `50.00` to `99.99` (5,000 levels)
- Ask price range: `100.01` to `150.00` (5,000 levels)
- Orders rest until cancelled.
- FIFO priority is maintained at each price level.
- Order IDs are never reused.
- Cancelling an unknown order ID returns `false`.

### Interface Changes

- Prices use `int32_t` ticks to eliminate floating-point errors and enable direct array indexing.
- Quantities use `int64_t` for portability across Windows and Linux.
- `add_order` returns `bool`: `false` for duplicate IDs, non-positive quantities, or out-of-range prices.
- Order book depth uses `size_t`.

## Build and Run

```bash
make clean && make
make test
./bench
./bench --impl A 
```

All four implementations pass the 8,000-operation differential test and the 1M-operation pre-check.

Every implementation finishes with the same final state:

| Metric   | Value |

| Best bid | `99.99 x 2035` |
| Best ask | `100.01 x 3091` |
| Checksum | `7527252290097286818` |

## Complexity Analysis

| Operation | A: `std::map` | B: Priority Queue | C: Fixed Arrays | D: Segment Tree |

| Add       | `O(log L)` | `O(log H)` for a new level; otherwise `O(1)` | `O(1)` | `O(log P)` for a new level; otherwise `O(1)` |
| Cancel    | `O(1)` + `O(log L)` if the level empties | `O(1)`, with lazy cleanup | `O(1)` + scan if the best level empties | `O(1)` + `O(log P)` if the level empties |
| BBO | `O(1)` | Amortized `O(1)` | `O(1)` | `O(log P)` |

**Notation:**

- `L` = number of active price levels.
- `P` = number of price slots per side (`5,000`).
- `H` = current heap size.

BBO stands for **Best Bid and Offer**.

## Benchmark Results

**Workload:** 1 million operations, comprising 70% adds, 20% cancels, and 10% BBO queries, with depth-10 queries every 100 operations.

- **Seed:** `0xC0FFEE`
- **Measurement:** Median of 5 runs
- **Compiler optimization:** `-O2`
- **Execution:** Single-threaded

### Performance Comparison

| Metric          | A     |     B   |     C     | D     |

| Total time (ms) | 672.3 | 527.4 | 339.0 | 357.5 |
| Nanoseconds per add | 620 | 343 | 260   | 271 |
| Nanoseconds per cancel | 834 | 551 | 481| 490 |
| Nanoseconds per BBO | 43 | 78 | 39 | 65   |
| Page faults | 27,336 | 27,402 | 29,090 | 29,090 |

**Result:** Implementation C achieves the lowest total runtime at `339.0 ms`, followed by D at `357.5 ms`.

### Hardware Counter Limitations

Hardware performance counters for cycles, instructions per cycle (IPC), branch misses, and cache misses were unavailable because WSL2 on an Intel 12th Gen host did not expose the PMU.

Consequently, only software counters, including `task-clock` and `page-faults`, were collected using:

```bash
perf stat -r 5
```

## Performance Analysis

### Why Implementation C Is Fastest

Implementation C uses fixed-size, price-indexed arrays. With a bounded price range, it can locate a price level using direct indexing and avoid the overhead associated with tree rebalancing, heap sifting, and per-level allocation.

This design delivers:

- Constant-time price-level access.
- No tree rebalancing.
- No heap sifting for best-price lookup.
- Predictable memory usage and straightforward auditing.

### Why Implementation D Is Close Behind

The segment tree provides efficient updates and best-price queries over a fixed price range. Its performance is close to C, although maintaining the tree introduces additional work compared with direct array access.

### Why Implementations A and B Are Slower

- **A — `std::map`:** Adding or removing price levels can involve node allocation, deallocation, pointer chasing, and tree rebalancing.
- **B — Priority queues:** Lazy deletion avoids immediate removal from heaps, but stale entries may need to be popped before the best valid price can be returned.

### Memory and Cache Considerations

Implementations C and D

## Conclusion

This project compares four limit order book implementations under a shared interface and a deterministic workload.

The fixed-size array implementation (C) achieved the lowest median runtime of **339.0 ms**, followed by the segment tree (D) at **357.5 ms**, the heap with lazy deletion (B) at **527.4 ms**, and the `std::map` implementation (A) at **672.3 ms**.

These results demonstrate that data structure selection should be guided by workload characteristics and constraints. The bounded price range makes direct indexing particularly effective in this project, while the segment tree provides an alternative for efficient ordered-price queries.

All four implementations passed the differential tests and the million-operation benchmark pre-check, producing matching checksums. The results are specific to the tested environment and workload; different price distributions and operation patterns may produce different performance rankings.
