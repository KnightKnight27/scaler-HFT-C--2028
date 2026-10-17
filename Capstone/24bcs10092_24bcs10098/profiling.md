# Linux perf profiling

Daksh Shah — 24bcs10092 — shah.24bcs10092@sst.scaler.com
Tirth Bhalani — 24bcs10098 — tirth.24bcs10098@sst.scaler.com

These whole-process user-space perf measurements were collected on
17 October 2026. Replay-only benchmark results are in benchmark.md.

## Profiling mode and fixed workload

bench now accepts --profile-only. This branch constructs and replays only the
selected implementation; it never runs the standard untimed A/B/C validation
loop. Standard mode retains its original cross-validation and measurement paths.

Profile-only supports only ops=1000000, seed=42 and runs=1 (runs defaults to 1
when omitted). Other inputs are rejected before trace generation. This restriction
makes the expected outcome an explicit fixed benchmark fixture, not a fabricated
oracle for arbitrary seeds. It validates trace fingerprint 1002121172284863879,
checksum 12750139682901687748 and BBO bid 9999:2988 / ask 10001:2344. The checksum
also consumes final full-depth snapshots. Add/cancel result checking and all
query checksum work are preserved.

Normal and profile-only runs of A/B/C were executed before profiling and agreed
on exact trace lines and fixture outputs. Every trace contains 700000 adds,
200000 live cancellations, 100000 primary BBO calls, and both depth(10) calls
every 100 operations (10000 per side); 349694 bid adds, 350306 ask adds, and
500000 final live orders. Profile-only logs contain no untimed_validation lines.
All 78 deterministic cases passed in the standard Release build. Negative CLI
checks rejected unsupported profiling seed, count, multiple runs and duplicate flag.

## Build, environment and exact collection commands

Ubuntu 26.04 LTS, x86_64 WSL2, kernel 6.6.114.1-microsoft-standard-WSL2;
Intel Core i3-1115G4 model @ 3.00GHz, four guest logical CPUs. GCC 15.2.0
(Ubuntu 15.2.0-16ubuntu1), CMake 4.2.3, /usr/bin/perf version 7.0.14.
perf_event_paranoid remained 2 and kptr_restrict remained 1. No sudo, sysctl,
security-setting changes, kernel installation or package installation was used.

From the repository root:

```sh
cmake -S Capstone/24bcs10092_24bcs10098 -B Capstone/24bcs10092_24bcs10098/build -DCMAKE_BUILD_TYPE=Release
cmake --build Capstone/24bcs10092_24bcs10098/build
ctest --test-dir Capstone/24bcs10092_24bcs10098/build --output-on-failure
cmake -S Capstone/24bcs10092_24bcs10098 -B Capstone/24bcs10092_24bcs10098/build-profile -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer"
cmake --build Capstone/24bcs10092_24bcs10098/build-profile
cd Capstone/24bcs10092_24bcs10098
sh scripts/profile.sh
```

The separate profiling build retains -std=c++17 -O2 -DNDEBUG and adds debug
symbols/frame pointers for readable stacks. It is not the original the replay benchmark
measurement binary. The committed script gives exact repeated commands for
A, B and C; each commands one selected replay per process and runs sequentially.
For each implementation, the main stat command is:

```sh
perf stat -r 5 -x ';' -o build-profile/evidence/stat-A.csv \
  -e 'task-clock,cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u,L1-dcache-load-misses:u,dTLB-load-misses:u,page-faults' \
  -- ./build-profile/bench --impl A --ops 1000000 --runs 1 --seed 42 --profile-only
```

Replace A with B or C in both output path and --impl. Hardware :u explicitly
excludes kernel events; software events were also labelled :u by perf under
current permissions. Generic hardware/cache/software names were inspected with
`perf list --raw-dump hw cache software` and tested before collection.

Supplementary five-repeat paired events provide aligned denominators:

```sh
perf stat -r 5 -x ';' -o build-profile/evidence/paired-A.csv \
  -e '{branches:u,branch-misses:u},{cache-references:u,cache-misses:u},{L1-dcache-loads:u,L1-dcache-load-misses:u},{dTLB-loads:u,dTLB-load-misses:u}' \
  -- ./build-profile/bench --impl A --ops 1000000 --runs 1 --seed 42 --profile-only
```

Pairs are grouped; the main requested event set is ungrouped. Repetition counts
are perf -r 5; returned counters are repeat means, not five raw per-run values.
Raw CSV also records perf's relative variation and running percentages. Rate
tables use the paired collection, not a mixture of different measurements.

Actual recording/reporting commands, repeated for each implementation:

```sh
perf record -g -F 499 -e cycles:u -o build-profile/evidence/perf-A.data \
  -- ./build-profile/bench --impl A --ops 1000000 --runs 1 --seed 42 --profile-only
DEBUGINFOD_URLS= perf report --stdio --no-children --call-graph none --sort dso,symbol --percent-limit 0.5 \
  -i build-profile/evidence/perf-A.data
DEBUGINFOD_URLS= perf report --stdio --children --call-graph graph,1,caller --percent-limit 1 --max-stack 32 \
  -i build-profile/evidence/perf-A.data
```

## WSL/reporting issue and workaround

The initial A counter and cycle-record commands succeeded. Its call-graph report
then remained asleep with near-zero CPU for over a minute while the environment
had DEBUGINFOD_URLS=https://debuginfod.ubuntu.com configured. The task-owned
report/collection processes were stopped, and the same existing data immediately
reported successfully with DEBUGINFOD_URLS empty. The complete collection was
then restarted with that variable cleared only in the script's environment.
No global settings changed. Initial partial collection logs remain ignored local
artifacts; result tables below use only the final complete collection. Some libc
frames consequently remain hexadecimal addresses rather than resolved names.

## What perf actually includes

These are whole-process counters and user-space cycle samples, not enabled-window
replay-only measurements. Perf includes dynamic startup, deterministic trace
creation and RNG, trace audit and its temporary hash set, selected book construction,
one selected replay, per-operation steady_clock instrumentation, checksum work,
result checks, final BBO/full-depth queries, output, book and trace destruction.
No other book implementation is constructed or replayed in this mode.

Trace/setup and destruction are separated from the benchmark's own replay timer,
but not from perf. They can be substantial for A/C and are visible in the profiles.
Generic STL/allocator/main symbols must not automatically be attributed solely
to order-book mutations. Ratios and counters use the entire measured process.

Frame pointers and sampling perturb execution. There is no CPU affinity,
isolation, controlled frequency, or Windows-host-load control. Virtualized PMU
semantics and WSL scheduling can differ from native Linux. User-only counts omit
kernel CPU work; task-clock/page faults and hardware cycles are different domains.
Call stacks through system libraries without frame pointers/symbols may be
incomplete. Samples and self/children percentages are statistical, not exact
operation durations; children percentages overlap and must not be summed.
Binary perf.data files and full reports are ignored build artifacts. Small text
evidence excerpts and raw counter CSV are retained under profiling/ for review.

## Five-repeat requested-event results

Counts below are perf repeat means; task-clock is milliseconds. All requested
events returned numeric readings and 100.00% running time. None was unavailable
or multiplexed; the paired groups also reported 100.00%. N/A would be used for
unavailable events, rather than zero. Exact CSV including variation is retained
in profiling/stat-{A,B,C}.csv and profiling/paired-{A,B,C}.csv.

| Event | A mean | B mean | C mean |
| --- | ---: | ---: | ---: |
| task-clock:u | 927.43 | 7503.3 | 1008.88 |
| cycles:u | 2742728721 | 24286259149 | 2240560964 |
| instructions:u | 1870216217 | 13815335963 | 1822711449 |
| branches:u | 358663027 | 3418510404 | 340723828 |
| branch-misses:u | 9196919 | 514191239 | 3699938 |
| cache-misses:u | 17987221 | 48617311 | 17334891 |
| L1-dcache-load-misses:u | 40651669 | 310654661 | 31635222 |
| dTLB-load-misses:u | 3526316 | 8201544 | 2962785 |
| page-faults:u | 30809 | 28728 | 30692 |

Perf-reported relative variation for cycles: A 2.62%, B 1.43%, C 2.89%. Cache misses: A 1.87%, B 9.92%, C 1.53%. These describe this five-repeat collection, not universal error bounds.

## Ratios from actual paired event means

Each rate is 100 * (mean misses / mean denominator) from the same grouped pair,
not an average of five separately computed rates or a mix with the main stat pass.
All pairs had 100.00% running time, so no multiplex scaling adjustment was needed.

| Implementation | Branch misses / branches | Generic cache misses / references | L1-data load misses / loads | dTLB load misses / loads |
| --- | ---: | ---: | ---: | ---: |
| A | 2.556% | 64.376% | 8.547% | 0.723% |
| B | 15.056% | 15.602% | 8.362% | 0.236% |
| C | 1.077% | 71.545% | 7.241% | 0.691% |
## Branch/cache/TLB/page-fault interpretation

- Branch rate means retired branch-misses / retired branches (all branch types),
  not conditional branches alone. B's 15.056% rate and much larger branch count
  accompany its sorting-heavy profile. A's 2.556% and C's 1.077% are whole-process
  rates; they are not isolated add/cancel rates.
- Generic cache-misses / cache-references is the generic last-level-cache-style
  perf event pair on this CPU (perf describes cache-misses as usually LLC misses).
  It is not the probability of a miss over every CPU memory access. C's 71.545%
  ratio exceeds A's despite similar absolute counts and lower cycles; B's 15.602%
  ratio is lower despite more absolute misses and much more work. Their differing
  denominators prevent interpreting this ratio alone as a performance ranking.
- L1-data load misses / L1-data loads describes the L1 load event pair, not LLC
  misses. B's absolute L1 misses are much higher while its 8.362% rate is close
  to A's 8.547%; its paired load denominator is 3,743,868,067 versus A's 472,419,816.
  This is consistent with substantially more load work during depth sorting.
- dTLB load misses concern address-translation caching, not data-cache misses.
  B has a lower rate (0.236%) but more absolute misses than A/C because its load
  count is much larger. A miss may require page-table translation without an OS
  page fault; it does not imply disk I/O.
- Software page-faults count OS page-fault events in the measured process domain;
  these differ from CPU cache and TLB misses and do not distinguish minor/major
  faults here. A/B/C means were 30809/28728/30692. They include startup, trace
  creation/audit and fresh allocations; the zero replay-only deltas after warmed
  validation in the replay benchmark are a different scope and are not contradicted.

These are observed associations plus architectural interpretation, not proof
that one particular code path caused each miss. Native Linux repeat measurements
and tighter phase windows would strengthen causal and performance conclusions.

## Actual sampled hotspots and call chains

All three records and both report variants succeeded as the normal Ubuntu user.
The final perf record logs report 463 samples for A, 5700 for B and 503 for C.
All reports state Total Lost Samples: 0. Sampling requested 499 Hz on cycles:u;
these are sampled event-weighted self/children percentages, not wall-time shares.

| Implementation | Observed self hotspot | Self overhead |
| --- | --- | ---: |
| A | OrderBookA::add_order | 27.80% |
| A | OrderBookA::cancel_order | 11.79% |
| A | main | 9.07% |
| B | std::__introsort_loop (PriceLevel comparator instantiation) | 43.66% |
| B | OrderBookB::depth | 43.57% |
| B | OrderBookB live-locator hash lookup (find) | 3.69% |
| C | main | 12.29% |
| C | OrderBookC::cancel_order | 10.79% |
| C | OrderBookC::add_order | 9.19% |
| C | __vdso_clock_gettime | 4.98% |

A also has live-index/history hash insertion and list-hook symbols among its
hotspots. C also has hash insertion and unresolved libc addresses; unresolved
addresses are not relabelled as specific allocation functions. B's depth and
sort symbols sum to 87.23% self overhead in this one profile, which directly
supports the required depth-copy/sort path as its dominant CPU work. Inlining
means some sorting instructions are attributed to depth itself.

Readable observed call-chain paths include:

```text
A: main -> replay -> OrderBookA::add_order -> map/tree insertion
B: main -> replay -> OrderBookB::get_asks -> OrderBookB::depth -> sort -> __introsort_loop
C: main -> replay -> OrderBookC::add_order -> live-locator unordered_map insertion
```

The inclusive replay FUNCTION entries report A 53.97%, B 97.21%, C 46.29%.
That function also includes result recording/checks and final snapshots outside
the chrono loop; these percentages are not enabled-window replay-only figures.
Substantial A/C cost lies outside that function, so whole-process hardware
counts cannot be treated as order-book-only cost per operation. The B stack
still clearly reaches its depth sorting; no A/C depth sorting was introduced.

Local text excerpts are in profiling/report-{A,B,C}-{self,stacks}.txt. They trim
line ends and abbreviate very long symbol names at 240 characters, labelled in
the files; full original reports remain in build-profile/evidence. The sample counts above come from the original perf record logs in the ignored
build-profile/evidence directory. Small A/C sample counts and unresolved system frames limit fine-grained conclusions.

## Outcome and scope audit

The complete final script reports exit 0 for every requested stat pass, paired
stat pass, recording and report. All 30 counter-run processes (five requested
plus five paired, per implementation) and all three recording processes printed
the benchmark fixture PASS; none printed an untimed cross-implementation pass.
No event was unsupported, no multiplexing was reported, and no kernel security
restriction was disabled. The separate standard-mode runs also passed their
A/B/C validations. These measurements use the same workload as the replay benchmark.
