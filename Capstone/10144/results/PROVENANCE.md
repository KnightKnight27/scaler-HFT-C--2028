# Evidence provenance

All results in this submission were produced in the user's local Linux workspace,
not CI or remote hardware. Environment is captured in environment.txt. Source is
original to this task; other students' implementations were not read.

Official benchmark records are `instrumented-<rep>-<impl>.txt`, 15 invocations,
rotated order, five per book. Each has UTC date, command, stdout, stderr, status;
CSV and JSON are derived from those records. All use one untabulated warmup on a
separate fresh book. Per-metric medians are calculated independently and need
not all belong to the same run. `instrumented-metadata.json` records SHA-256 for
every measured source/build file and the binary, with actual compiler commands.
These source identities, not an invented or circular future commit hash, anchor
the measurements. Evidence and source are committed together after validation.

The million-operation correctness run uses the same default generator as the
benchmark. Debug (-O0) and sanitizer (-O2, ASan+UBSan) runs execute all deterministic/sweep/bounded suites
plus a 10000-primary-operation trace; they are not performance measurements.
The extra Release seed executes all suites plus 100000 primary operations.

`perf/commands.json` records dated real invocations and statuses. The assignment
full-event `perf stat -r 5` command is executed for A/B/C. Raw stdout/stderr are
separate and unedited. Perf averages and deviations are not benchmark medians.
On this hybrid system, CPU0 is a core-PMU CPU; atom PMU lines marked not-counted
are not zeros and do not enter the table. Perf may scale multiplexed counts:
actual percentages remain visible in stderr. IPC and branch percentages use
ratios of mean counts from the same collection group, not a mean of five ratios.

External profiler scope includes startup, workload generation, internal timers,
query checksums, replay and destruction. Internal replay timings exclude startup,
generation, book construction/destruction, and output. Do not equate these scopes.
Machine-specific `*.data` files remain local and are ignored; readable reports
are committed. Any supplementary bulk results are separately labelled and must
never replace or be pooled with the required instrumented results.
