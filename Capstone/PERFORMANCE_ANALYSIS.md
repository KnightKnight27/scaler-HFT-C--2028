# Performance Analysis: Low-Latency Order Book

## 1. Objective

The objective of this experiment is to compare three single-threaded order
book implementations written in C++17. Each implementation uses a different
data structure to manage price levels and orders.

The implementations were benchmarked using an optimized build with the
compiler flag `-O2`. The same workload and fixed random seed were used to
make the comparison more consistent.

## 2. Benchmark Methodology

The benchmark workload contains 1,000,000 operations and uses random seed
12345. The approximate operation distribution is:

- 70% add-order operations
- 20% cancel-order operations
- 10% best-bid-and-offer (BBO) queries

Top-10 bid and ask queries are also performed every 100 operations.
The benchmark uses `std::chrono::steady_clock`, runs five times, and reports
the median runtime.

Using the same workload and compiler optimization settings helps compare
the implementations under similar conditions. However, results may vary
with the processor, operating system, compiler, and system load.

## 3. Benchmark Results

The recorded median runtimes were:

| Implementation | Data Structure | Median Runtime |
|---|---|---:|
| A | Ordered map and hash containers | 968.96 ms |
| B | Sorted vectors | 3108.55 ms |
| C | Fixed-size arrays | 551.77 ms |

### Relative Performance

Implementation C was approximately 1.76 times as fast as Implementation A
and 5.63 times as fast as Implementation B, based on these recorded median
runtimes.

Implementation A was approximately 3.21 times as fast as Implementation B.

These ratios describe the measured workload; they are not guarantees for
every possible workload.

## 4. Analysis of Each Implementation

### Implementation A: Ordered Map

Implementation A uses `std::map` to maintain price levels in sorted order.
It also uses hash-based containers to track live orders and previously used
order IDs.

The ordered map makes it convenient to access the best bid and best ask.
However, tree-based operations involve navigation through the data structure,
which can require more work than direct array indexing.

The recorded median runtime was 968.96 ms.

### Implementation B: Sorted Vectors

Implementation B stores price levels in sorted vectors. Vectors provide
contiguous storage, which can be beneficial for memory locality.

However, inserting or removing an element may require shifting other elements
to preserve sorted order. When price levels change frequently, these shifts
can add substantial overhead.

The recorded median runtime was 3108.55 ms, the highest of the three
implementations for this benchmark.

### Implementation C: Fixed-Size Arrays

Implementation C uses fixed-size arrays for the supported price range.
A price level can be accessed directly using its index.

This avoids the need to search an ordered tree or shift sorted-vector
elements when accessing or changing a price level. It is particularly
suitable for this project because the valid price range is bounded.

The recorded median runtime was 551.77 ms, the lowest of the three
implementations.

The result suggests that direct indexing was advantageous for the workload
and price range used in this experiment.

## 5. Correctness and Consistency

The separate correctness tests check important behaviours such as adding
orders, aggregating quantities at the same price, rejecting invalid input,
rejecting duplicate IDs, and cancelling orders.

All three implementations also produced the same recorded benchmark
checksum. This provides a useful consistency check across the runs.

However, matching checksums alone does not prove complete correctness.
Correctness tests and review of edge cases remain important.

## 6. Profiling Limitations

The `perf` tool was available on the system, but attempts to collect hardware
performance counters and call-graph profiles failed because the system
restricted access to performance-monitoring events.

Consequently, this experiment does not report measured CPU cycles, cache
misses, or function-level profiling results. The performance discussion is
based on the recorded benchmark runtimes and the data structures used.

## 7. Conclusion

Under the tested workload, Implementation C was the fastest, Implementation
A ranked second, and Implementation B was the slowest.

The fixed-size array design benefited from direct indexing within a bounded
price range. The ordered-map design offered sorted price levels without
requiring vector shifts. The sorted-vector design incurred additional costs
when elements had to move to preserve ordering.

The experiment demonstrates that data-structure selection can have a
significant effect on order-book performance. The best choice depends on
price-range constraints, operation patterns, memory requirements, and the
workload being measured.