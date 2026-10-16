# results/

Raw outputs produced on the authors' machine. Every number in `docs/report.md` and `docs/report_details.md` traces back
to a file here. Nothing in this folder is typed in or edited by hand.

## Layout

| Folder            | What it is                                                                                                                                      | Use in the report                                                                                                                                            |
|-------------------|-------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `wsl/`            | **Measurement set.** Ubuntu 26.04 under WSL2 (kernel 6.18.33.2-microsoft-standard-WSL2), i7-13650HX, g++ 15.2.0, perf 7.0.14, 2026-10-10.       | Timing, page faults, task-clock, `perf record` hot spots, sanitizer pass.                                                                                    |
| `windows-dryrun/` | Earlier dry run on Windows 11 / MinGW g++ 16.2.0, 2026-10-10. `steady_clock` granularity 100 ns.                                                | Not a source for the results table. Kept because it shows the same C per-op vs total discrepancy on a different clock (see the report's data-quality notes). |
| *(native Linux)*  | **Not available.** No native Linux machine could be used, so hardware counters (cycles, instructions, branches, cache, dTLB) were not measured. | Those rows are `n/s` in the report; substitute evidence is in the report §2.3.                                                                               |

### Known limits of `wsl/`

- **Software counters only.** The WSL2 kernel logs `Performance Events: unsupported CPU family 6
  model 183 no PMU driver, software events only.` Every hardware counter is `<not supported>`
  (`wsl/perf_unsupported.txt`). Only `task-clock` and `page-faults` were measured.
- `perf_event_paranoid = 2`: user-space counting only (events carry `:u`).
- `perf record` sampled with the software CPU clock (~400–1,200 samples per run), not
  hardware cycles. The rankings are usable, but small percentages are not.
- `perf.data.{A,B,C}` are in `~/orderbook-perf-data/` inside WSL, not in the repo: perf
  cannot write them to a Windows drive under `/mnt` (mmap → "Bad address").

### `windows-dryrun/` caveats

- `bench_summary.txt` contains lines from **two** script runs (5 lines instead of 3; the order is
  B, A, C, B, C). Only the A line and the *second* B and C lines match the raw `bench_*.txt` files.
- `verify.txt` is UTF-16 (written by a PowerShell `>` redirect).

## Files (per measurement set)

| File                                         | Produced by                                                                      |
|----------------------------------------------|----------------------------------------------------------------------------------|
| `verify.txt`                                 | `./build/bench --verify > …/verify.txt`                                          |
| `sanitizer.txt`                              | `build/edge_tests_asan` + `build/bench_asan --verify` (`scripts/build.sh asan`)  |
| `bench_{A,B,C}.txt`, `bench_summary.txt`     | `scripts/run_bench.sh` (5 fresh processes per impl)                              |
| `perf_env.txt`                               | `scripts/run_perf.sh` (machine, CPU, kernel, perf version, flags)                |
| `perf_stat_{baseline,A,B,C}.txt`             | `scripts/run_perf.sh` (`perf stat -r 5`)                                         |
| `perf_stat_*_stdout.txt`                     | the program's own output during those perf stat runs                             |
| `perf_unsupported.txt`                       | counters perf reported as `<not supported>` / `<not counted>`                    |
| `PERF_SUMMARY.md`                            | `scripts/perf_summary.sh` (table + IPC / miss-rate formulas, from the raw files) |
| `perf_report_{A,B,C}.txt`, `*_callgraph.txt` | `perf report` on the `perf record --call-graph fp` samples                       |

### Extra files in `wsl/` (substitutes for the missing hardware counters)

| File                                                         | Produced by                                                                                                                                      |
|--------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------|
| `annotate_{A,B,C}.txt`                                       | `perf annotate --stdio --percent-limit 2` on `~/orderbook-perf-data/perf.data.{A,B,C}` (per-instruction samples of the `-O2` build)              |
| `bench_batch_{A,B,C}.txt`, `bench_summary_batch.txt`         | `TAG=batch BENCH_ARGS="--batch" scripts/run_bench.sh`: clock-perturbed pass, and a block-timed least-squares fit (R² ≈ 0: the fit is not usable) |
| `bench_scrambled_{A,B,C}.txt`, `bench_summary_scrambled.txt` | `TAG=scrambled BENCH_ARGS="--ids scrambled" scripts/run_bench.sh`: **non-graded** variant                                                        |
| `bench_thin_{A,B,C}.txt`, `bench_summary_thin.txt`           | `TAG=thin BENCH_ARGS="--mix thin" scripts/run_bench.sh`: **non-graded** variant                                                                  |
| `latency_{A,B,C}.txt`                                        | `./build/bench --impl X --runs 1 --latency`: per-op p50/p99/p99.9/max (one run each, clock overhead not subtracted)                              |
| `inprocess_{A,B,C}.txt`                                      | `./build/bench --impl X --runs 5 --total-only`: checks whether repeated runs in one process drift (they did not under WSL2)                      |

The batch and variant runs use a `bench` binary rebuilt after `--batch`, `--mix` and `--ids`
were added. The graded code path is unchanged, and the graded workload checksum is identical.
