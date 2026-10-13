# Performance Analysis

## Fastest implementation

Implementation C is fastest overall and for add, cancel, and BBO in this run. Its fixed arrays avoid tree node traversal and keep price-level access contiguous. Maintaining the best bid and ask makes normal BBO access a scalar lookup. Implementation A is second overall: ordered maps are slower than arrays but still provide direct best-level iterators. Implementation B is slowest here, especially for BBO, because each query copies and cleans a heap to preserve const-correctness.

## Branches and cache behavior

Hardware branch and cache counters were unavailable because `perf` is not installed. The code structure still predicts different behavior. Map nodes and heap storage involve pointer and indirect accesses, while the array implementation uses contiguous fixed storage. The heap also performs stale-entry checks and comparator-driven branching. These are hypotheses only; they must be confirmed on Linux with `perf stat`.

## Page faults and memory

No page-fault measurements were available. The array design allocates its price storage in one object and has predictable capacity. The map and heap designs allocate dynamic nodes and heap buffers as the workload grows, so they may touch more allocation-backed pages.

## Production choice

For this fixed single-instrument assignment, C is the production choice: it has the lowest measured latency, bounded price storage, predictable best-price access, and simple cancellation lookup. A would be preferable if the price domain became dynamic or sparse enough that fixed arrays wasted meaningful memory. B is not the choice here because its lazy cleanup adds complexity and its measured BBO path is substantially slower.

## Next experiments

- Install or run on a Linux lab machine and collect the required `perf` counters.
- Measure distributions and tail latency, not only medians.
- Compare integer tick arrays with a bitmap or next-occupied-index structure.
- Pre-size the order-ID hash table and measure allocation effects.
- Separate benchmark timing overhead from the very small BBO operation.
