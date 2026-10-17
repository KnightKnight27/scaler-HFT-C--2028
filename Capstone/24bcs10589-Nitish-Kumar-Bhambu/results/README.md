# Final results: C is the unhinted ring-buffer slot pool

A = sorted vectors; B = ordered maps; C = fixed order slots + free-slot ring,
ID-to-slot hash table and price arrays. All measurements here use the current
C++17 final sources, default capacity 600000. Earlier array-only C results are
archived outside the submission and must not be mixed with these measurements.

- `environment.txt`: compiler, flags, machine, CPU affinity and measured source hashes.
- `correctness.txt`: directed API checks, ring-specific tests and full-depth comparisons.
- `sanitizer.txt`: the same verification under Clang ASan/UBSan, exit code zero.
- `benchmark_*.txt`: five million-operation runs, individual and median timings.
- `perf_stat_*.txt`: assignment event list, five one-replay processes.
- `perf_core_*.csv`: explicit P-core counters; means of five one-replay processes.
- `perf_workload_*.txt`, `perf_core_workload_*.txt`, `perf_record_workload_*.txt`:
  actual profiled workloads and their checksums.
- `perf_report_*.txt`, `perf_hotspots_*.txt`: call graphs and sampled self-time reports.
- `memory_*.txt`: one additional million-operation replay measured with /usr/bin/time.
- `summary.json`, `summary.csv`: derived medians, counters and ratios.

All 60 chrono/profiling replays agree on checksum 10326099508913568648. Each
has 700471 adds, 199304 cancels, 100225 BBO queries, and both depth-10 snapshots
every 100 operations. P-core counters had 100% running time; generic E-core
events were not counted and the E-core L1 event was unsupported on this setup.
Profiles reported zero lost samples. Empty stderr files mean no extra diagnostics.

Main chrono timing excludes construction/destruction and generation. A separate
setup-inclusive timer includes book construction/destruction and per-run output
and bookkeeping, but not generation. Whole-process perf includes all of these.
Individual operation times include clock and dispatch overhead. Peak RSS is for
the complete process, not just the book.

Optimized flags: C++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic.
Sanitizer flags: C++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer.
