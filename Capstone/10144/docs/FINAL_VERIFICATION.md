# Final verification and skeptical review

The complete supplied assignment was read before design: 17 pages, SHA-256
`170bec341b143f844775a1ad62ed30f80bb71e5a969f6171ebd7c0b734cec5c2`.
The design note was written before source implementation. The upstream default
branch was queried as `main`; the authenticated account has no upstream write
permission, so the existing NoteLegend fork and isolated submission directory
are used. The course README, Capstone notes, repository structure and applicable
instructions were inspected. No AGENTS, contribution policy, PR template or CI
workflow was present. Other students' implementation files were not read.

## Separate skeptical source review

Reviewed the final source as a grader, with these material findings and repairs:

1. An integer price parameter alone permits implicit C++ floating conversion.
   Deleted floating-price overloads now make those calls fail compilation. Nine
   compile-negative cases (A/B/C times float/double/long double) passed.
2. Early verifier diagnostics constructed strings on passing checks. Literal
   checks and BBO diagnostics now construct exceptions/details only on failure.
   Coverage was retained. Earlier long-running checks were stopped and replaced
   with passing checks on the updated verifier, rather than used as evidence.
3. The unoptimized sanitizer sweep was stopped for excessive test-infrastructure
   runtime, without a sanitizer diagnostic. The same complete coverage was rerun
   with ASan/UBSan at -O2; Debug -O0 was checked separately.
4. A smoke check caught a CSV parser returning a DictReader rather than its row.
   Repaired before collecting official measurements; reran CLI checks successfully.

Semantic review: no matching, trades or threading; all five APIs exist; all bands
and side validation occur before indexing; integer quantity overflow is checked
before addition; absent BBO fields are deterministic. Live ID duplicates are
rejected; historical uniqueness remains the documented caller contract. IDs
never index arrays. C's bit shifts are 0..63 and padding bits never become set.

Lifetime review: A holds no hash iterator across insertion/rehash; B's price
vector iterators are not retained across mutation, and ID-tree insertion cannot
invalidate them; C stores numeric slots, not vector pointers. Swap-removal updates
the moved order's hash entry before erasing the cancelled ID. Allocation failures
roll back inserted live orders where needed. C has no custom hash or tombstones.

Complexity review: three different order/level layouts, no virtual hot-path calls;
expected/amortized hash/growth costs and worst cases are explicit. C's word/bit
scans are not called asymptotically constant. Retained capacities are described
using peak live orders, not an unbounded insertion history. Default capacity is
used for all books; fixed arrays use only the allowed price-domain knowledge.

Measurement review: pre-generated identical trace; unbiased live-ID selection;
all scheduled cancels must succeed. Exactly 1M primary operations plus 20K depth
calls. Internal per-call versus total versus process-wide perf scopes are
separated. Query outputs are consumed; checksum/final BBO agreement is checked.
Five independent metrics' medians come from five rotated sequential warmed runs.
Actual compile commands use C++17, -O2, debug symbols and frame pointers. No -O3,
CPU governor, privilege or machine security changes are used.

## Actual checks

- Release million-op seed42 and extra 100000-op seed123456: PASS, including all
  bounded suites, with complete logs in `results/correctness-*.txt`.
- Debug: PASS all deterministic/sweep/random suites plus 10000 primary operations.
- Compile rejection of floating prices: PASS, `results/float-rejection.txt`.
- CLI: PASS help, ten bad-argument cases, six A/B/C mode/warmup smokes, live cancel
  and checksum/depth agreement, `results/cli-smoke.txt`.
- Fresh source-only out-of-tree Release build: PASS the default million-op verify,
  extra seed command, floating rejection and all smokes; actual flags checked;
  `results/fresh-build.txt`. Temporary source and build trees were removed afterward.

- ASan/UBSan (-O2) with leak detection: PASS all suites plus 10000 primary
  operations, no diagnostics; actual flags in `validation-compile-commands.json`.
- Five official instrumented repetitions per book: PASS; every warmup and measured
  checksum/final BBO/trace agreed, and every scheduled cancel succeeded.
- Required profiler collection: PASS all 19 commands, including full `stat -r5`,
  split counter checks, `record -g`, and readable reports for A/B/C. Requested
  core events were available. Unused atom PMU entries were not counted and its L1
  event unsupported; no unavailable value was replaced with zero.
- Supplementary bulk run/profiles: PASS five separate runs per book and 12
  profiler/report commands. Bulk averages are deliberately unmeasured/zero in
  CSV and are not included in the official per-call table.
- Every stat stdout contains exactly five one-million-operation replays with the
  same trace/checksum and 200311 successful cancels, checked automatically in
  `results/final-evidence-checks.txt`. All reports have zero lost samples.
- Table regeneration: PASS medians independently recomputed from 15 raw runs;
  IPC/branch percentages use comparable full-command mean counts.
- Report: PASS `pdfinfo` confirms two pages; `pdftotext` confirms table and all six
  analysis answers; both pages rendered with pdftoppm and visually inspected,
  with no clipping or overlap. ReportLab is only an optional document dependency.
- Source/binary SHA-256: PASS current files match validation, both measurement
  metadata files, and the exact measured binary. No measured code/build changes
  followed collection. No stale commit hash is asserted for measurements.
- All relative Markdown links, Python syntax, shell syntax and diff whitespace:
  PASS for authored source/docs; raw profiler/measurement whitespace is preserved
  verbatim and excluded using submission-local .gitattributes. README commands correspond to actual checked invocations; local perf
  prerequisite and exact workspace-local invocation are documented.

Profiling scope is user-only, process-wide. No reduced running percentages appear
for active core counters. Atom entries and unresolved outer startup call-chain
frames are disclosed. Flat symbol reports supplement call graphs; no claims
attribute generator/clock work exclusively to book operations. No memory or tail
latency guarantee is claimed. Required work is ready for review.

## GitHub submission confirmation

[PR #132](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/132) was
created and inspected via direct GitHub API calls. It is OPEN and not a draft.
Destination: `KnightKnight27/scaler-HFT-C--2028:main`; source:
`NoteLegend/scaler-HFT-C--2028:capstone/10144-order-book`. The pushed source commit
was confirmed against the local branch. The paginated files API confirms all
145 changed files are inside `Capstone/10144/`; no builds, profiling data or
course files are included. GitHub reports mergeable state `clean`. No CI checks
are configured (`statusCheckRollup` is empty); this is not a claimed CI pass.
The submission was neither merged nor self-approved. No required work remains
blocked. This final documentation update leaves measured code, flags and evidence
unchanged; source/binary hash identities remain the provenance anchor.
