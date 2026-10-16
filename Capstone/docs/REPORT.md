# Order Book: Results and Analysis

**Authors:** Shreyas S [shreyas.24bcs10401@sst.scaler.com](mailto:shreyas.24bcs10401@sst.scaler.com) 24BCS10401

**Setup.** Intel i7-13650HX laptop, Ubuntu 26.04 under WSL2, g++ 15.2 `-std=c++17 -O2
-DNDEBUG`, perf 7.0.14. Graded workload: 1,000,000 ops (seed 42; 70% add, 20% cancel, 10% BBO)
plus 10,000 depth-query pairs. Times are medians of 5 fresh processes (12 ns clock). All
three books match a naive oracle on every operation, also under ASan/UBSan. WSL2 exposes no
hardware counters, so IPC, branch and cache figures are *not supported*; 11.2–11.3 use
substitute evidence instead. Supporting data: `docs/report_details.md`, raw files in
`results/wsl/`.

|                                               | A: `map` levels + `unordered_map` | B: sorted vector + `map<id>` | C: array + bitmap + open addressing |
|-----------------------------------------------|-----------------------------------|------------------------------|-------------------------------------|
| Total time (ms)                               | 160.89                            | 183.65                       | **27.39**                           |
| ns / add · cancel · BBO                       | 134.8 · 302.9 · 15.2              | 113.2 · 463.2 · 17.8         | ≤95.0 · ≤82.5 · 14.2                |
| ns / depth pair                               | 126.7                             | **53.3**                     | 81.6                                |
| p99.9 add · cancel (ns, one run)              | 625 · 4,744                       | 1,156 · 13,064               | 440 · 394                           |
| Page faults above generation baseline (8,932) | 7,424                             | 7,891                        | 8,234                               |
| Total, scrambled ids (ms, non-graded)         | 214.16 (×1.33)                    | 508.65 (×2.77)               | 32.09 (×1.17)                       |
| IPC, branch-miss %, cache misses              | n/s                               | n/s                          | n/s                                 |

**What the workload exercises.** The book saturates early: by op ~161k all 5,000 levels per
side are occupied, and none empties in the second half of the run (the best level emptied
only 6 times in 1M ops). Level creation and removal, and C's search for a new best price, are
therefore almost never timed. The benchmark mostly compares lookups, and its ids are
sequential, which flatters trees and hash tables (see the scrambled-id row).

**11.1 Fastest.** C, by total time: 5.9× faster than A and 6.7× faster than B. We rely on total
time because timing each operation distorts C. Its per-op means sum to 85.2 ms against a
27.39 ms total, and adding one clock read between operations slows C by ~71 ns/op while
leaving A and B unchanged. C's per-op figures are therefore upper bounds. B has the slowest
cancel: `std::map<id>::find` takes 32.5% of its samples. Contrary to our expectation, A's add
is not allocation-bound (`malloc` + `free` ≈ 1.2% of samples); its two level-tree lookups
take 21.9%. C wins because each operation is one hash probe that almost always hits the
first slot, plus a direct array index, with no allocation. B is fastest for depth queries
because its levels are contiguous. BBO is O(1) everywhere and sits at the clock's noise floor.

**11.2 Branch misses.** These could not be counted under WSL2. The compiled code shows where
mispredictions can occur: every hot lookup loop is compare-and-branch, with no `cmov`. A's
level lookup (about 13 levels) and B's order-tree descent (about 19 levels) branch on random
prices and ids, so their decisions are close to unpredictable. C's probe exits at the first
slot at a load factor of at most 0.24, which should predict well. We therefore expect several
mispredictions per operation for A and B and few for C. This is an expectation, not a
measurement.

**11.3 Cache behaviour.** This is indirect evidence (no counters). B's order tree (~501k
56-byte nodes) and C's 32 MiB hash table both exceed the 24 MiB L3. B pays ~19 *dependent*
loads per cancel, and 83% of its cancel samples sit behind a node load. C pays one scattered
slot load per operation. The clock-perturbation result suggests C's independent loads overlap
when operations run back-to-back (memory-level parallelism), while A's and B's dependent
chains cannot. Scrambling ids hurts B most (×2.77); a likely cause, untested, is lost
key-order locality between its tree nodes.

**11.4 Page faults.** The counts are within 11% of each other, so they mainly reveal *when*
memory is touched. C's 8,234 extra faults are its 32 MiB table (8,192 pages), zero-filled in
the constructor before timing. B's work out to 64.5 B per live order, matching a 56-byte node
rounded to 64 B, and A's to 60.7 B. Both fault while growing during the timed run. That split
is inferred from the code, not separately measured.

**11.5 Deployment.** We would deploy **C**, provided the price band stays fixed and narrow. It is
~6× faster, has the best tails (cancel p99.9 394 ns against 4,744 and 13,064 ns; worst single
operation 0.16 ms against 8.3 ms for A, which is plausibly a hash rehash, and 1.1 ms for B), and
is the least sensitive to id order. Its costs are a fixed 32 MiB footprint, twice A's code, and
a deletion algorithm whose bugs only targeted tests caught. If the band were wide or unknown,
we would choose **A** with a pre-reserved hash table. B's contiguous levels help only depth
queries, while its `std::map<id>` slows every cancel.

**11.6 Next steps.** Obtain hardware counters on native Linux, and attribute faults with
`perf record -e page-faults`. Measure C's per-type costs with type-pure streams (per-op timing
and block regression both failed). Reserve A's table to test the 8.3 ms outlier. Replace B's
`std::map<id>` with a flat hash table. Try huge pages and next-slot prefetching for C.
Benchmark the thin-book and cancel-at-best cases as labelled stress tests.
