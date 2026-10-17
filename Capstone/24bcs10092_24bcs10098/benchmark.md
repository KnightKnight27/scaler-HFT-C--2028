# Order book benchmark

Team: Daksh Shah (24bcs10092) and Tirth Bhalani (24bcs10098).
Recorded on 17 October 2026. These are preliminary instrumented WSL results;
no Linux perf profiling or screenshots have been collected in this stage.

## Build and commands

From the repository root in Ubuntu WSL:

```sh
cmake -S Capstone/24bcs10092_24bcs10098 -B Capstone/24bcs10092_24bcs10098/build -DCMAKE_BUILD_TYPE=Release
cmake --build Capstone/24bcs10092_24bcs10098/build --verbose
ctest --test-dir Capstone/24bcs10092_24bcs10098/build --output-on-failure
cd Capstone/24bcs10092_24bcs10098
./build/bench --help
./build/bench --impl A --ops 1000 --runs 2 --seed 42
./build/bench --impl B --ops 1000 --runs 2 --seed 42
./build/bench --impl C --ops 1000 --runs 2 --seed 42
./build/bench --impl A --ops 1000000 --runs 5 --seed 42
./build/bench --impl B --ops 1000000 --runs 5 --seed 42
./build/bench --impl C --ops 1000000 --runs 5 --seed 42
```

The mandatory commands run sequentially A, B, C, without concurrent benchmark
jobs. Full raw console output is included below when the runs complete. Ignored
build logs are local supporting artifacts, not required submission dependencies.

## Exact trace and CLI contract

Exactly 1,000,000 primary operations: 700,000 adds, 200,000 live cancellations,
100,000 BBO calls. After primary operations 100, 200, ..., 1,000,000, call both
get_bids(10) and get_asks(10): exactly 10,000 additional calls per side.
The 20,000 depth calls are not deducted from the primary quota.

One mt19937_64 engine, seed 42 by default, generates each invocation's trace
once, outside timers. Explicit rejection sampling produces unbiased bounded
integers without depending on uniform_int_distribution implementations. Remaining
add/cancel/BBO quotas weight each next choice. If a cancellation is chosen with
an empty live set, use a remaining add instead and leave the cancellation quota
pending. Thus exact quotas are preserved and every cancellation targets a live
ID. The workload has more adds than cancels, so this repair always has an add
available. A live-ID vector supports uniform selection and swap/pop removal.

Adds sample side with equal probability, prices uniformly within the side's
5,000 valid ticks, and quantity uniformly 1..100. IDs increase from zero and
never repeat. Integer ticks are stored directly in the trace. An independent
untimed trace audit checks legal fields, monotonic IDs, live cancels and quotas.
A deterministic fingerprint includes operation kind, ID and add fields.
The same const trace object is used for every replay within an invocation;
matching fingerprints verify identical traces across the separate CLI commands.

--impl is required and accepts only A/B/C. Default ops/runs/seed are 1000000/5/42.
Custom counts use adds=floor(7*N/10), cancels=floor(2*N/10), BBO=N-adds-cancels;
depth calls per side=floor(N/100). Limits are N=10..2000000 and runs=1..25 to
reject nonsensical allocation requests before trace creation. Seed accepts any
unsigned 64-bit decimal value. Negative, malformed, out-of-range, missing,
unknown and duplicate options fail with nonzero exit and usage text. Bad allocation
also produces a nonzero failure; the limits are not a promise of available RAM.
Even-run medians average the two middle values. No workload is silently reduced.

## Timing and outcome validation

A book is constructed empty before each replay and destroyed after timing.
Trace generation/audit, book construction/destruction, result-record allocation,
independent outcome checks and printing are outside the overall replay timer.
Successful add/cancel result enums are recorded in preallocated storage during
replay and checked afterwards; any rejection reports operation index, kind and ID.
Before timed runs, untimed A/B/C passes compare query checksums, final BBO and
full final aggregated depth. Every timed replay is checked against that outcome.

steady_clock measures the whole primary loop including required depth queries.
Individual sections surround each add/cancel/BBO and each bid/ask depth call.
Their accumulated durations divided by actual counts give per-run means; the
report then takes the median of those means, not a median of individual operation
latencies. Overall time and total depth time also have their own five-run medians.
Each metric's median may come from a different replay.

Overall time includes virtual dispatch, trace traversal, timer calls, duration
accumulation, result recording, checksum mixing, loop bookkeeping and query-vector
destruction. Per-call sections include query snapshot creation but end before
checksum consumption and destruction of returned depth vectors. All implementations
use the same boundaries, no LTO or architecture-specific build flags, and -O2.

A 64-bit unsigned rolling checksum consumes operation types/results, every primary
BBO, every required depth result, and final BBO/full depth. It is printed and
compared so queries remain observable. Post-replay final snapshots and their
checksum work are outside timing. These extra validation queries do not count
as timed workload calls. Hash agreement is not proof against collisions; the
Independent reference-model verification supplies additional correctness
evidence. This benchmark does not rerun the full random oracle inside timing.

## Measurement limitations and fairness

The clock reports steady=true and nominal tick=1 ns. This is a representation
period, not measured effective resolution or clock-read cost. Two reads per timed
section perturb short operations; no baseline is subtracted. Means include timer
and surrounding control overhead and are not pure isolated operation latency.
There are no latency percentiles or claims of production HFT latency.

Every run starts with a new empty book, but allocator pages, caches and OS state
are not cold. Untimed validation runs A/B/C before the selected implementation's
five runs, providing implicit warmup with order/cache bias. There is no separate
calibrated warmup, affinity pinning, CPU isolation, frequency control or cache
flushing. WSL/Windows scheduling, host load and frequency/thermal changes can
influence these preliminary medians. Fixed A/B/C command order can also bias them.

B copies and sorts occupied aggregate levels on every depth call. All 20,000
required depth calls remain enabled; their separate timings expose that cost.
Depth cost and mutation cost should not be conflated when interpreting overall
runtime. The data structures were not modified during benchmarking.

Linux getrusage(RUSAGE_SELF) supplies real counters. process_peak_rss_kib is the
process-lifetime RSS high-water mark in KiB; it includes trace generation, trace
audit, all three untimed validation passes, allocator retention and accumulated
result snapshots. It is neither a selected-book peak nor a per-run incremental
allocation measurement. Values across A/B/C invocations therefore do not isolate
their data-structure memory usage. ru_minflt/ru_majflt deltas span each replay loop,
excluding construction, destruction and post-replay validation. Allocator reuse
can make later fault counts small or zero. Non-Linux builds explicitly report
resource counters unavailable (zero placeholders); the recorded runs use Linux.

## Observed environment

- Ubuntu 26.04 LTS, x86_64, Microsoft WSL2 full virtualization.
- Kernel: 6.6.114.1-microsoft-standard-WSL2.
- CPU model: 11th Gen Intel Core i3-1115G4 @ 3.00GHz (reported model string;
  actual run frequency was not measured).
- Guest topology: 4 logical CPUs, 2 cores, 2 threads/core, one socket.
- Reported caches: L1d 96 KiB/2 instances, L1i 64 KiB/2, L2 2.5 MiB/2, L3 6 MiB.
- GCC: g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0.
- CMake 4.2.3; C++17 Release compile flags -O2 -DNDEBUG -Wall -Wextra -Wpedantic.
- Guest memory snapshot before runs: 3.7 GiB total, 3.2 GiB available; 1 GiB swap,
  zero swap used. Load averages: 0.17, 0.23, 0.43. These are snapshots, not a
  continuous monitor or a claim that the Windows host was otherwise idle.
- Executables/project are on /mnt/d (Windows-backed workspace); trace and book
  replay perform no explicit file I/O inside timing.

## Smoke and robustness checks

All A/B/C smoke commands above completed with OUTCOME_CONSISTENCY=PASS.
For 1000 operations: 700 adds, 200 cancels, 100 BBO, 10 depth calls per side;
345 bid adds, 355 ask adds, 500 final live orders.
Trace fingerprint: 15269236758378005143; checksum: 7166924230721076296.
Final BBO in ticks/quantity: bid 9989:20, ask 10003:12.

Custom rounding smoke (C, N=101, runs=1, seed=7) passed with 70/20/11 primary
counts and one depth call per side. Minimum workload smoke (A, N=10, runs=1,
seed=UINT64_MAX) passed with 7/2/1 counts and no depth calls (reported n/a means).
Eleven invalid CLI combinations were correctly rejected: invalid implementation,
zero/excessive ops, zero/excessive runs, negative/overflow seed, missing value,
missing implementation, duplicate option and unknown option.

The preserved deterministic verifier passed 78/78 cases via CTest (one test
executable), with no changes to the verifier or A/B/C implementations.
## Mandatory one-million-operation results

All three commands completed five timed replays and untimed A/B/C validation.
Each trace had 700000 adds, 200000 cancels, 100000 primary BBO calls and 10000
calls per depth side. Actual side sampling: 349694 bid adds and 350306 ask adds.
Final live count: 500000. All commands printed the same trace fingerprint
1002121172284863879 and outcome checksum 12750139682901687748.
Final BBO in integer ticks/aggregate quantity: bid 9999:2988, ask 10001:2344.
All 15 timed runs and all nine untimed validation passes agreed, including full
final depth. The logs were independently checked for exactly five runs per
command and matching fingerprints/checksums.

### All five overall replay times (ms)

| Implementation | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 | Median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A | 410.493 | 427.198 | 429.377 | 505.199 | 434.972 | 429.377 |
| B | 6581.979 | 6737.518 | 7635.996 | 6781.859 | 6673.300 | 6737.518 |
| C | 332.365 | 275.147 | 356.875 | 268.192 | 327.892 | 327.892 |

### Median per-run operation means (ns) and total depth time

| Implementation | Add mean ns | Cancel mean ns | BBO mean ns | Bid depth mean ns | Ask depth mean ns | Total depth median ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| A | 430.053 | 466.571 | 26.339 | 238.591 | 177.017 | 4.156 |
| B | 505.433 | 1247.221 | 55.127 | 304154.792 | 302171.741 | 6063.265 |
| C | 267.270 | 490.410 | 46.780 | 253.174 | 183.636 | 4.388 |

B's required depth sorting dominates this workload's total, and is retained in
all reported timings. C's lower observed total is a result on this instrumented
WSL trace, not a universal ranking. Differences in BBO means are especially
sensitive to timer overhead. Independent medians should not be added together
to reconstruct overall runtime or the total depth median.

### OS counters (scope as described above)

| Selected implementation | Median process high-water RSS KiB | Maximum observed process high-water RSS KiB | Median replay minor faults | Median replay major faults |
| --- | ---: | ---: | ---: | ---: |
| A | 117884 | 118140 | 0 | 0 |
| B | 117320 | 117320 | 0 | 0 |
| C | 117504 | 117760 | 0 | 0 |

All 15 replay fault deltas were actually zero. Untimed validation and allocator
reuse preceded measurement; these are not cold-start fault measurements or
zero-allocation claims. High-water RSS includes the untimed passes of the other
implementations and must not be interpreted as a per-book memory comparison.

## Raw mandatory-run console output

### Implementation A

```text
config impl=A ops=1000000 runs=5 seed=42 C++=201703 compiler=15.2.0
os=Linux 6.6.114.1-microsoft-standard-WSL2 x86_64
RSS scope: process lifetime high-water KiB, includes trace and untimed validation; faults: per-replay deltas, construction/destruction excluded.
clock steady=1 nominal_tick_ns=1.000
trace adds=700000 cancels=200000 bbo=100000 bid_adds=349694 ask_adds=350306 depth_calls_per_side=10000 final_live=500000 fingerprint=1002121172284863879
untimed_validation impl=A PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
untimed_validation impl=B PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
untimed_validation impl=C PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=1 overall_ms=410.493 add_mean_ns=408.560 cancel_mean_ns=463.533 bbo_mean_ns=31.578 bid_depth_mean_ns=208.963 ask_depth_mean_ns=155.584 depth_total_ms=3.645 process_peak_rss_kib=117500.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=2 overall_ms=427.198 add_mean_ns=429.093 cancel_mean_ns=466.571 bbo_mean_ns=23.418 bid_depth_mean_ns=260.655 ask_depth_mean_ns=178.530 depth_total_ms=4.392 process_peak_rss_kib=117628.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=3 overall_ms=429.377 add_mean_ns=430.053 cancel_mean_ns=459.767 bbo_mean_ns=26.679 bid_depth_mean_ns=238.591 ask_depth_mean_ns=177.017 depth_total_ms=4.156 process_peak_rss_kib=117884.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=4 overall_ms=505.199 add_mean_ns=517.458 cancel_mean_ns=535.320 bbo_mean_ns=26.339 bid_depth_mean_ns=253.309 ask_depth_mean_ns=192.566 depth_total_ms=4.459 process_peak_rss_kib=118012.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=5 overall_ms=434.972 add_mean_ns=436.054 cancel_mean_ns=488.560 bbo_mean_ns=23.285 bid_depth_mean_ns=213.997 ask_depth_mean_ns=162.761 depth_total_ms=3.768 process_peak_rss_kib=118140.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
median overall_ms=429.377 add_mean_ns=430.053 cancel_mean_ns=466.571 bbo_mean_ns=26.339 bid_depth_mean_ns=238.591 ask_depth_mean_ns=177.017 depth_total_ms=4.156 process_peak_rss_kib=117884.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
OUTCOME_CONSISTENCY=PASS
```

### Implementation B

```text
config impl=B ops=1000000 runs=5 seed=42 C++=201703 compiler=15.2.0
os=Linux 6.6.114.1-microsoft-standard-WSL2 x86_64
RSS scope: process lifetime high-water KiB, includes trace and untimed validation; faults: per-replay deltas, construction/destruction excluded.
clock steady=1 nominal_tick_ns=1.000
trace adds=700000 cancels=200000 bbo=100000 bid_adds=349694 ask_adds=350306 depth_calls_per_side=10000 final_live=500000 fingerprint=1002121172284863879
untimed_validation impl=A PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
untimed_validation impl=B PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
untimed_validation impl=C PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=1 overall_ms=6581.979 add_mean_ns=505.433 cancel_mean_ns=1247.221 bbo_mean_ns=55.214 bid_depth_mean_ns=296972.079 ask_depth_mean_ns=296250.717 depth_total_ms=5932.228 process_peak_rss_kib=117320.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=2 overall_ms=6737.518 add_mean_ns=503.019 cancel_mean_ns=1315.302 bbo_mean_ns=117.709 bid_depth_mean_ns=304154.792 ask_depth_mean_ns=302171.741 depth_total_ms=6063.265 process_peak_rss_kib=117320.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=3 overall_ms=7635.996 add_mean_ns=537.850 cancel_mean_ns=1619.214 bbo_mean_ns=55.127 bid_depth_mean_ns=344957.563 ask_depth_mean_ns=344252.659 depth_total_ms=6892.102 process_peak_rss_kib=117320.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=4 overall_ms=6781.859 add_mean_ns=532.223 cancel_mean_ns=1237.515 bbo_mean_ns=52.589 bid_depth_mean_ns=307802.737 ask_depth_mean_ns=303986.930 depth_total_ms=6117.897 process_peak_rss_kib=117320.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=5 overall_ms=6673.300 add_mean_ns=502.668 cancel_mean_ns=1241.356 bbo_mean_ns=54.280 bid_depth_mean_ns=301810.579 ask_depth_mean_ns=301036.179 depth_total_ms=6028.468 process_peak_rss_kib=117320.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
median overall_ms=6737.518 add_mean_ns=505.433 cancel_mean_ns=1247.221 bbo_mean_ns=55.127 bid_depth_mean_ns=304154.792 ask_depth_mean_ns=302171.741 depth_total_ms=6063.265 process_peak_rss_kib=117320.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
OUTCOME_CONSISTENCY=PASS
```

### Implementation C

```text
config impl=C ops=1000000 runs=5 seed=42 C++=201703 compiler=15.2.0
os=Linux 6.6.114.1-microsoft-standard-WSL2 x86_64
RSS scope: process lifetime high-water KiB, includes trace and untimed validation; faults: per-replay deltas, construction/destruction excluded.
clock steady=1 nominal_tick_ns=1.000
trace adds=700000 cancels=200000 bbo=100000 bid_adds=349694 ask_adds=350306 depth_calls_per_side=10000 final_live=500000 fingerprint=1002121172284863879
untimed_validation impl=A PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
untimed_validation impl=B PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
untimed_validation impl=C PASS checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=1 overall_ms=332.365 add_mean_ns=278.179 cancel_mean_ns=490.410 bbo_mean_ns=47.856 bid_depth_mean_ns=253.174 ask_depth_mean_ns=199.452 depth_total_ms=4.526 process_peak_rss_kib=117376.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=2 overall_ms=275.147 add_mean_ns=219.794 cancel_mean_ns=441.222 bbo_mean_ns=39.717 bid_depth_mean_ns=207.917 ask_depth_mean_ns=153.329 depth_total_ms=3.612 process_peak_rss_kib=117376.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=3 overall_ms=356.875 add_mean_ns=289.405 cancel_mean_ns=533.140 bbo_mean_ns=69.979 bid_depth_mean_ns=287.241 ask_depth_mean_ns=230.752 depth_total_ms=5.180 process_peak_rss_kib=117504.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=4 overall_ms=268.192 add_mean_ns=211.834 cancel_mean_ns=433.930 bbo_mean_ns=40.483 bid_depth_mean_ns=197.692 ask_depth_mean_ns=144.544 depth_total_ms=3.422 process_peak_rss_kib=117632.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
run=5 overall_ms=327.892 add_mean_ns=267.270 cancel_mean_ns=519.733 bbo_mean_ns=46.780 bid_depth_mean_ns=255.155 ask_depth_mean_ns=183.636 depth_total_ms=4.388 process_peak_rss_kib=117760.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
median overall_ms=327.892 add_mean_ns=267.270 cancel_mean_ns=490.410 bbo_mean_ns=46.780 bid_depth_mean_ns=253.174 ask_depth_mean_ns=183.636 depth_total_ms=4.388 process_peak_rss_kib=117504.000 replay_minor_faults=0.000 replay_major_faults=0.000 checksum=12750139682901687748 final_bbo=9999:2988 10001:2344
OUTCOME_CONSISTENCY=PASS
```
## Final harness verification

The benchmark itself was additionally compiled with ASan/UBSan and exercised
with --impl C --ops 1000 --runs 2 --seed 42 (this also runs untimed A/B/C checks).
It exited 0 with matching smoke checksum and no sanitizer diagnostics. Its
instrumented timings are not included in the preliminary measurement tables.

```sh
g++ -std=c++17 -O2 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -I . \
  impl_a/orderbook_a.cpp impl_b/orderbook_b.cpp impl_c/orderbook_c.cpp \
  bench.cpp -o build/bench_sanitized
./build/bench_sanitized --impl C --ops 1000 --runs 2 --seed 42
./build/verify --random --ops 1234 --seed 42
```

The verifier again passed all 78 deterministic cases plus
1234 reference-model operations: 842 adds, 277 cancels, 115 BBO, three full-depth/
invariant checkpoints. No implementation, shared API or verifier code was changed during benchmarking.
No failures or compiler warnings were observed. The previous 100,000-operation
randomized verification runs were not repeated for these benchmark checks.
