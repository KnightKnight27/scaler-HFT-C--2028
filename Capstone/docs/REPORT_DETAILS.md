# Order Book: Supporting Evidence

Detailed measurements and checks behind the analysis in `docs/report.md`. All figures come from `results/wsl/` (and, where stated, `results/windows-dryrun/`).

**Authors:** Shreyas S [shreyas.24bcs10401@sst.scaler.com](mailto:shreyas.24bcs10401@sst.scaler.com) 24BCS10401

## 1. Setup

|                        |                                                                                                                                                                                                                                                                                  |
|------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Machine / CPU          | Intel Core i7-13650HX (13th gen, 20 logical CPUs; L2 12.5 MiB, L3 24 MiB), laptop                                                                                                                                                                                                |
| OS / kernel            | Ubuntu 26.04 LTS under **WSL2**, kernel `6.18.33.2-microsoft-standard-WSL2`                                                                                                                                                                                                      |
| perf                   | perf 7.0.14, `perf_event_paranoid = 2` (user-space events only). **Software counters only**: the WSL2 kernel has no PMU driver for this CPU, so every hardware counter is `<not supported>` (`results/wsl/perf_unsupported.txt`)                                                 |
| Compiler, flags        | g++ 15.2.0 (Ubuntu), `-std=c++17 -O2 -DNDEBUG`; `perf record` used the same plus `-g -fno-omit-frame-pointer`                                                                                                                                                                    |
| Clock                  | `steady_clock` granularity 12 ns, empty-pair overhead 14.2–14.7 ns across the 15 processes (`Clock:` lines in `results/wsl/bench_*.txt`)                                                                                                                                         |
| Transparent huge pages | `madvise` (not `always`), so the program's allocations use 4 KB pages unless it requests huge pages; it does not                                                                                                                                                                 |
| Workload               | 1,000,000 ops, `mt19937(42)`, hash `27f23594045a1e72`; 700,471 add / 199,303 cancel / 100,226 BBO; + 10,000 depth-query pairs every 100 ops (extra, timed separately)                                                                                                            |
| Repetition             | Timing: median of 5 separate processes (`scripts/run_bench.sh`). Counters: `perf stat -r 5`.                                                                                                                                                                                     |
| Correctness            | `results/wsl/verify.txt`: A, B, C (and C from a 16-slot table) identical to the oracle on every op, graded + thin-book workloads. `results/wsl/sanitizer.txt`: same checks under AddressSanitizer + UBSan, 0 errors. All 15 timed processes printed checksum `cfa9e743f13cd8a8`. |

Methodology details: README "Benchmark methodology" and "Profiling".

## 2. Results

Timing from `results/wsl/bench_summary.txt` (raw: `bench_{A,B,C}.txt`); counters from
`results/wsl/perf_summary.md` (raw: `perf_stat_*.txt`). Counters are per process and
**include workload generation, book construction and destruction**. The baseline column
(`bench --generate-only`) shows the generation share.

| Metric                                      | Baseline (generation only) | A               | B               | C               |
|---------------------------------------------|----------------------------|-----------------|-----------------|-----------------|
| Total time (ms), median of 5                | n/a                        | 160.89          | 183.65          | 27.39           |
| Total time range (min–max, ms)              | n/a                        | 156.59–169.74   | 178.70–186.75   | 26.31–29.54     |
| ns / add                                    | n/a                        | 134.8           | 113.2           | 95.0            |
| ns / cancel                                 | n/a                        | 302.9           | 463.2           | 82.5            |
| ns / BBO                                    | n/a                        | 15.2            | 17.8            | 14.2            |
| ns / depth pair (bids+asks, 10 levels each) | n/a                        | 126.7           | 53.3            | 81.6            |
| task-clock (ms, whole process, mean of 5)   | 68.46 (±2.20%)             | 239.95 (±1.38%) | 295.08 (±1.04%) | 112.73 (±1.29%) |
| IPC (instructions / cycles)                 | n/s                        | n/s             | n/s             | n/s             |
| Branch misses (% of branches)               | n/s                        | n/s             | n/s             | n/s             |
| L1-dcache-load-misses                       | n/s                        | n/s             | n/s             | n/s             |
| Cache misses (% of cache refs)              | n/s                        | n/s             | n/s             | n/s             |
| dTLB-load-misses                            | n/s                        | n/s             | n/s             | n/s             |
| Page faults                                 | 8,932                      | 16,356          | 16,823          | 17,166          |
| Page faults minus baseline                  | n/a                        | 7,424           | 7,891           | 8,234           |

n/s = not supported. Counters not supported on this machine: `cycles`, `instructions`,
`branches`, `branch-misses`, `cache-references`, `cache-misses`, `L1-dcache-load-misses`,
`dTLB-load-misses` (all of them under WSL2; `results/wsl/perf_unsupported.txt`).
No native Linux machine was available, and the WSL2 kernel exposes software events only
(`Performance Events: … no PMU driver, software events only`). These rows are therefore
reported as not supported rather than estimated. Section 2.3 lists
the substitute evidence used instead.

Big-O per implementation: README → `docs/decisions.md`.

### 2.1 Data-quality notes (facts about the measurements)

1. **Per-op vs total consistency.** (median ns/op × op counts, summed over add, cancel, BBO
   and depth) divided by the median total time:

   |   | Per-op × counts (ms) | Measured total (ms) | Ratio    |
   |---|----------------------|---------------------|----------|
   | A | 157.6                | 160.89              | 0.98     |
   | B | 173.9                | 183.65              | 0.95     |
   | C | 85.2                 | 27.39               | **3.11** |

   The same check on the Windows dry run (100 ns clock, `results/windows-dryrun/`) gave A 1.03,
   B 0.92, C 2.72. C's per-op pass and total pass disagree on both machines and both clocks.
   The per-op pass puts a clock pair around every op; the total pass does not.
2. **task-clock ≠ total time.** perf counts the whole process: generation (baseline 68.46 ms),
   the workload hash, `count_ops`, book construction (C zero-fills its 32 MB table here) and
   destruction (A and B free ~500k nodes). Use `bench_summary.txt` for the timed region.
3. **The BBO numbers sit near the noise floor:** 14–18 ns per op against a 12 ns clock tick
   and a ~14 ns overhead that has already been subtracted.
4. **Profiles are coarse:** sampled with the software CPU clock (A 908, B 1,171, C 416
   samples). They also include the harness: `hash_workload` + `generate_workload` account
   for 21.6% of A's samples, 16.0% of B's and 51.0% of C's.

### 2.2 Profile hot spots (from `results/wsl/perf_report_*`)

Percentages are of all samples in the process. "Incl." values come from the call-graph
reports, where most book code is inlined into its caller.

| Impl | Where the book's samples land                                                                                                                                                                                                                                                                                                                                                                                              |
|------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| A    | `apply<BookA>` 28.4% self, mostly the add path: level-map lookups `std::map<int,…>::operator[]` → `_M_lower_bound` 12.0% + 9.9% incl. (bid and ask maps). `BookA::cancel_order` 26.2% self, of which hash bucket search `_M_find_before_node` 10.8% incl. `_Hashtable::_M_insert_unique_node` 7.1%. `malloc` 0.55%, `cfree` 0.66%, plus 1.87% in unresolved libc addresses (only entries ≥ 0.5% are listed in the report). |
| B    | `BookB::cancel_order` 35.6% self, of which `std::map<id>::find` 32.5% incl. `apply<BookB>` 19.9% (add: `lower_bound` on the level vectors 5.5% + 6.1% incl., order-tree `emplace` 6.4% incl.). `_Rb_tree_rebalance_for_erase` 7.9%. `_Rb_tree::_M_erase` 3.2% is the tree destructor (outside the timed region). `cfree` 1.28%, unresolved libc addresses 2.65%.                                                           |
| C    | Book code is inlined into `main` (25.7% self), of which `BookC::insert_order` (add path) 22.8% incl. `BookC::cancel_order` 6.3% self, of which `find_slot` 4.6% incl. `BookC::reset_table` 2.2% is the constructor's 32 MB fill (outside the timed region).                                                                                                                                                                |

To resolve the unnamed libc addresses (they may include allocator internals), install
`libc6-dbg` in WSL and re-run `scripts/run_perf.sh`.

### 2.3 Substitute experiments (no hardware counters available)

**(a) Compiled code of the hot loops** (`results/wsl/annotate_{A,B,C}.txt`: `perf annotate`
of the `-O2` `bench_perf` binary, with software-clock samples per instruction). With
sampling, a sample is usually charged to the instruction just *after* the one that stalled.

| Hot loop                                                   | Share of the function's samples                                                                  | Loop control                         |
|------------------------------------------------------------|--------------------------------------------------------------------------------------------------|--------------------------------------|
| B: `std::map<id>::find` in `cancel_order`                  | 83% on the instruction after the load of each tree node (`mov 0x18(%rax)`)                       | `cmp` key, `jae`: conditional branch |
| B: `lower_bound` on the level vector                       | under 2%                                                                                         | `cmp`, `jg`: conditional branch      |
| A: level `std::map::lower_bound` on add (bid and ask maps) | 29.5% and 32.2% on the instruction after each node load                                          | `cmp`, `jge`: conditional branch     |
| A: `unordered_map` lookup in `cancel_order`                | 57% around the bucket-pointer load and the first chain-node load; bucket index via integer `div` | conditional branches                 |
| C: slot probe in `insert_order` (inlined into `main`)      | 88% on the instruction after the first load of the hashed slot                                   | `test`, `je`                         |
| C: slot probe in `cancel_order` (`find_slot`)              | 73% on the instruction after the first load of the home slot                                     | `test`, `je`                         |

None of these loops compiled to conditional moves (`cmov`); all are compare-and-branch.

**(b) Clock-perturbed pass** (`bench --batch`, `results/wsl/bench_batch_*.txt`, 5 processes
per impl). The same loop as the total pass, plus one `steady_clock::now()` after every op and
depth query (1,010,000 reads), each process compared with its own total pass:

|   | Unexplained slowdown per process (ms) | Median   | Per op   |
|---|---------------------------------------|----------|----------|
| A | −17.32, −3.58, 3.33, 5.15, 16.99      | 3.33 ms  | +3.3 ns  |
| B | −32.12, −11.73, −4.56, 3.37, 8.86     | −4.56 ms | −4.5 ns  |
| C | 55.21, 56.31, 71.57, 72.39, 74.72     | 71.57 ms | +70.9 ns |

"Unexplained" = perturbed total − plain total − (reads × calibrated cost per read). For A and
B it scatters around zero; for C it is positive in every process. This is the same direction
and size as note 2.1 (1) (C's per-op pass vs total pass).

**(c) Block-timed per-type estimates: failed, do not use.** `--batch` also timed 50-op blocks
and fitted per-type costs by least squares. The model explains almost none of the
block-to-block variation (R² per process: A 0.000 ×5; B 0.000–0.125; C 0.000–0.002), and it
gives impossible values (A: ns/BBO −298.4). Per-type costs therefore remain unresolved for C.
The plain per-op pass inflates them (note 2.1 (1)) and the block fit cannot separate them.

**(d) Non-graded workload variants** (`results/wsl/bench_summary_{scrambled,thin}.txt`;
5 fresh processes each; reported separately from the graded numbers):

| Total time, median ms (range)      | A                      | B                      | C                   |
|------------------------------------|------------------------|------------------------|---------------------|
| Graded (Section 2)                 | 160.89 (156.59–169.74) | 183.65 (178.70–186.75) | 27.39 (26.31–29.54) |
| Scrambled ids (`--ids scrambled`)  | 214.16 (192.52–238.54) | 508.65 (432.07–634.36) | 32.09 (30.88–34.37) |
| Thin book (`--mix thin`, 45/45/10) | 69.38 (67.26–85.86)    | 93.90 (92.24–95.89)    | 31.71 (30.33–34.89) |

- Scrambled ids replay the *same* book evolution (identical checksum `cfa9e743f13cd8a8`); only
  the id values differ, so the difference from graded is attributable to how each order store
  handles non-sequential ids.
- The thin-book run is a different workload (≈450k adds, ≈450k cancels, a book of a few
  hundred orders; checksum `4c8f102f23e21457`), so compare A, B and C *within* that row only.
- Per-op values for the variants are in the same files. C's per-op pass shows the same
  inflation as in 2.1 (1) (scrambled: ns/add 112.7 vs a total of 32.09 ms).

**(e) Per-op latency percentiles** (`./build/bench --impl X --runs 1 --latency`,
`results/wsl/latency_{A,B,C}.txt`; graded workload, one run each; non-graded bonus).
Raw ns per op with the clock overhead (~15 ns) **not** subtracted; clock granularity 12 ns.

| op         | percentile        | A                 | B                    | C               |
|------------|-------------------|-------------------|----------------------|-----------------|
| add        | p50 / p99 / p99.9 | 127 / 273 / 625   | 119 / 336 / 1,156    | 112 / 266 / 440 |
| add        | max               | 8,331,002         | 1,104,477            | 156,783         |
| cancel     | p50 / p99 / p99.9 | 264 / 629 / 4,744 | 571 / 1,366 / 13,064 | 116 / 266 / 394 |
| cancel     | max               | 788,355           | 306,778              | 46,853          |
| BBO        | p50 / p99 / p99.9 | 29 / 37 / 63      | 28 / 39 / 61         | 28 / 36 / 62    |
| depth pair | p50 / p99 / p99.9 | 131 / 396 / 1,364 | 70 / 169 / 362       | 94 / 371 / 750  |

Caveats for using these:
- They come from the per-op pass, which 2.1 (1) and 2.3 (b) show inflates C relative to its
  uninterrupted speed. Compare shapes (p50 → p99.9 → max) more than absolute values across
  implementations.
- A single run under WSL2 on a laptop: a max can include an interrupt, a scheduler preemption
  or a page fault, not only the book's own worst case. The max column says that one op took
  that long, not why. The run-to-run variability of these tails was not measured.
- Checksums matched (`cfa9e743f13cd8a8`), so these runs did the same work as the graded runs.

## 3. What the graded workload does and does not exercise

These are exact counts from replaying the op list through the oracle
(`bench --verify` prints them). They are not timings and do not depend on the machine.

| Graded workload property            | Value                                               |
|-------------------------------------|-----------------------------------------------------|
| Live orders at end                  | 501,168                                             |
| Non-empty price levels at end       | 5,000 / 5,000 bid, 5,000 / 5,000 ask                |
| Levels emptied during the run       | 3,845, all before op 160,755 (0 in the second half) |
| Cancels that emptied the best level | 6 (1 bid, 5 ask) in 1,000,000 ops                   |
| Order ids                           | sequential 1, 2, 3, …                               |

The graded workload saturates the book early. By operation ~161k every one of the 5,000
price levels on each side holds at least one order, and from then on no level empties
again: none of the 3,845 level-emptying events falls in the second half of the run, and a
cancel removed the last order at the best price only 6 times in 1,000,000 operations. The
code paths that distinguish the level structures under churn (A's tree-node insertion and
erasure, B's vector shifts and C's bitmap search for the next best level) are therefore
almost never timed. The benchmark mainly compares lookups: the level lookup on add and the
order lookup on cancel. Profiles support this for cancel. B's `std::map<id>::find` takes
32.5% of its samples against under 2% for its level search, and A's hash bucket search takes
10.8% against 3.4% for its level lookup. They do not fully support it for add: A's level-map
lookups take 21.9% of its samples, more than its hash insert (7.1%). Order ids are also
sequential, which is kind to trees and hash tables. Replaying the identical book evolution
with scrambled ids (same checksum) slows A by ×1.33, B by ×2.77 and C by ×1.17. The
scrambled-id and thin-book runs are non-graded variants, reported separately in §2.3.

## 4. Claim → evidence checklist (for the analysis in `docs/report.md`)

| Claim in the text                                            | Evidence (file + counter / symbol)                                                                    | Supported?                                                             |
|--------------------------------------------------------------|-------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------|
| C is fastest overall (total time)                            | `wsl/bench_summary.txt`: 27.39 vs 160.89 / 183.65 ms                                                  | Supported                                                              |
| C's per-op values are inflated by per-op timing              | report 2.1 (1): ratio 3.11; `wsl/bench_summary_batch.txt`: +71 ns/op perturbed                        | Supported (timing evidence)                                            |
| B's cancel cost is the order-tree search                     | `wsl/perf_report_B_callgraph.txt`: `std::map<id>::find` 32.5%; `annotate_B.txt`: 83% behind node load | Supported (software sampling)                                          |
| A's add is dominated by level-map lookups, not allocation    | `wsl/perf_report_A_callgraph.txt`: map `lower_bound` 21.9% vs malloc 0.55% / cfree 0.66%              | Supported; unnamed libc 1.87% (refutes the pre-measurement hypothesis) |
| Hot lookup loops compile to branches, not cmov               | `wsl/annotate_{A,B,C}.txt`                                                                            | Supported (code); miss counts not measured                             |
| Branch misses: how many                                      | none: `wsl/perf_unsupported.txt`                                                                      | Not measurable here                                                    |
| C's speed comes from overlapping independent memory accesses | `wsl/bench_summary_batch.txt` (perturbed) + `annotate_C.txt`                                          | Indirect only: inference, no cache counters                            |
| B depends on id order much more than A or C                  | `wsl/bench_summary_scrambled.txt`: ×2.77 / ×1.33 / ×1.17, same checksum                               | Supported; the locality explanation is a hypothesis                    |
| Page-fault counts are similar; C's are its 32 MiB table      | `wsl/perf_summary.md`: deltas 7,424 / 7,891 / 8,234 vs 8,192 pages                                    | Supported; timed vs untimed split inferred, not measured               |
| A's 8.3 ms max add is a rehash                               | `wsl/latency_A.txt`: one sample                                                                       | Plausible, unproven                                                    |
| Depth queries are cheapest in B (contiguous levels)          | `wsl/bench_summary.txt`: ns/depth A 126.7 / B 53.3 / C 81.6                                           | Supported (timing); the cause is an inference from the data structure  |
| C has the best tails (p99.9 add/cancel, max)                 | `wsl/latency_{A,B,C}.txt`: p99.9 cancel 4,744 / 13,064 / 394 ns; max add 8.3 / 1.1 / 0.16 ms          | Supported for one run each; per-op pass (C inflated); WSL noise        |
| A and B fault during the timed run, C before it              | code (`BookC::reset_table` in the constructor; heap growth in A/B) + `wsl/perf_summary.md`            | Inferred, not measured (no `perf record -e page-faults`)               |
| C stays fastest on the thin-book variant                     | `wsl/bench_summary_thin.txt`: 69.38 / 93.90 / 31.71 ms                                                | Supported (non-graded)                                                 |
| Deploy C under a fixed narrow band, otherwise A              | combines the rows above                                                                               | Authors' judgement: check that you agree                               |
