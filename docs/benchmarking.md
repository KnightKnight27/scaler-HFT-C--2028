# Benchmark notes

## Workload

The default run uses 1,000,000 operations from seed 42. Approximately 70% are
adds, 20% cancels, and 10% BBO queries. Every 100 operations also requests ten
levels from each side. Quantities range from 1 to 100; side and valid prices
are sampled uniformly.

Workload generation happens before timing. A generator-owned vector tracks
live IDs; cancellation chooses one and removes it with swap/pop. If a cancel
is drawn while the book is empty, it becomes an add. `std::mt19937` plus
rejection sampling gives repeatable bounded integers without relying on a
particular library's `uniform_int_distribution` implementation.

The program prints a workload hash and a result checksum. Matching workload
hashes establish that the input streams match; matching checksums are a useful
benchmark check, but do not replace the exhaustive verifier.

## Timing

Each normal run replays the workload twice on fresh books:

- The first pass measures total replay time with two `steady_clock` calls.
- The second pass times each add, cancel and BBO call separately, then divides
  each total by its operation count.

Both passes consume returned values into a checksum, including the periodic
depth results. Their checksums must agree. Depth queries and checksum work are
included in the total replay time, but not in individual operation timings.
Workload generation, initial book construction, destruction and output are
excluded from replay totals. No warm-up run is discarded.

Clock overhead remains in the individual measurements. This matters most for
BBO calls, whose cost is close to the clock's own overhead. Do not sum the
individual latencies to reconstruct the separately measured total.

Run each implementation with the default five repetitions:

```bash
./bench --impl A --csv results/A.csv
./bench --impl B --csv results/B.csv
./bench --impl C --csv results/C.csv
```

The program reports the median of each metric. The saved comparison used five
samples per implementation, pinned to CPU 0 and collected in rotating order
ABC/BCA/CAB. `results/benchmarks.csv` retains all 15 samples, with medians in
`results/summary.json` and machine details alongside them.

Turbo, CPU frequency and unrelated applications are not controlled. The first
samples were slower; they remain in the saved data.

## Profiling

With Linux perf installed, profile A using:

```bash
perf stat -r 5 \
  -e task-clock,cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u,L1-dcache-load-misses:u,dTLB-load-misses:u,page-faults \
  ./bench --impl A --profile --runs 1

mkdir -p build
perf record --all-user --user-callchains --no-inherit -e cycles:u \
  -c 1000000 -g -o build/perf_A.data -- ./bench --impl A --profile --runs 10
perf report --stdio --no-children -i build/perf_A.data
```

Repeat for B and C. `--profile` disables individual operation clocks while
retaining the final BBO and checksum. If an event is unsupported, omit it and
document the missing counter. To collect an event separately, for example:

```bash
perf stat -r 5 -x ';' -e cycles:u ./bench --impl A --profile --runs 1
```

Unlike replay totals, perf counts the entire process: generation, replays,
destruction and output. Call graphs use ten replays to collect more samples;
counter runs use one. The report's hardware counts are means from `perf stat
-r 5`, not timing medians. Independent-event output is retained in
`results/perf_events.txt`. IPC is instructions divided by cycles; branch-miss
percentage is 100 times branch misses divided by branches. The derived values
are saved in `results/counters.json`.

On this machine, all nine requested counters and user call graphs worked.
Default call-chain recording was initially denied; `--user-callchains`
excluded kernel stacks and fixed it without changing kernel settings.
Raw captures are in `build/`; portable text reports are in `results/`.
Re-record profiles after rebuilding if the old captures no longer match the
executable's build ID.
