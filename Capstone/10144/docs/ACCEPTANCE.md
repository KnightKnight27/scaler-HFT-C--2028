# Requirements and grading acceptance matrix

Assignment authority: complete locally supplied 17-page PDF (extracted before
design; actual PDF page count recorded in final verification). Original assignment
remains in the parent workspace, not copied into this submission. Statuses refer
to linked evidence; profiling restrictions are disclosed rather than presumed.

| Requirement / rubric | Implementation / explanation | Tests / evidence | Status |
|---|---|---|---|
| Correctness **35**: all five APIs | `include/book_{a,b,c}.hpp`, `include/types.hpp` | `results/correctness-*.txt` | PASS |
| Single instrument, standard library, C++17, single thread, resting only | all headers, CMakeLists, README | source review; no matching/trades | PASS |
| 0.01 ticks; exact buy/ask bands; midpoint excluded | `types.hpp::validate`, integer cents contract | all ticks, invalid bands/midpoint; `float-rejection.txt` | PASS |
| unique never reused arbitrary uint64 IDs | live lookup in all books; historical uniqueness caller precondition | zero, UINT64_MAX, sparse IDs; unknown/double cancels | PASS |
| aggregate quantity, partial/final/best cancellation | all book headers | deterministic edges and full-tick drain | PASS |
| deterministic absent fields, independent sides, ordering/depth | shared BBO, const depth APIs | edge cases; reference after every primary operation | PASS |
| invalid side, price, quantity, duplicate; overflow | validation before mutation, exception rollback | edge tests, INT64_MAX aggregate and failed-add state | PASS |
| compare cancellation/BBO/bid+ask each operation | `src/verify.cpp::Suite` | million seed42 top10 every step; full every10000/final | PASS |
| independent model and invariants | Reference ID ledger, combined map, independent array reconstruction | full after each bounded step incl 10000-tick fill/drain | PASS |
| additional random seeds, growth and slot repair | verifier bounded seeds0/7/2026, long seed123456 | saved Release, Debug, ASan+UBSan logs | PASS |
| Three genuinely different designs **20** | `docs/DESIGN.md`, A trees+hash; B vector+ID tree; C array+packed/index | headers, full API differential checks | PASS |
| design before code; complexity/costs | DESIGN; README complexity table incl worst/amortized, high-water capacities | self-review in FINAL_VERIFICATION | PASS |
| Clean benchmark/perf **25**: one executable, impl selector/help | `src/main.cpp`, CMakeLists | CLI error/smoke checks in final verification | PASS |
| exact -O2 optimized build | CMake explicit Release flags, symbols/frame pointers | metadata compile commands, clean rebuild | PASS |
| million primary ops, seed42, 70/20/10, uniform valid adds | workload.hpp independent unbiased live-ID generator | CSV actual counts + trace hash | PASS |
| live-only cancels, both depth calls each100 incl final | main replay loop, generator swap removal | success count checked; 20000 depth calls | PASS |
| timing boundaries, steady_clock, observable query consumption | main replay, README methods | raw checksum/final BBO agreement | PASS |
| 5 repetitions medians and all raw runs | scripts/measure.py, rotated order, fresh books and warmup | instrumented-runs.csv and medians.json | PASS |
| fair capacity/CPU/flags; no competing builds or tests | default book capacities; CPU0; identical trace; sequential scripts | metadata + environment + final verification | PASS |
| perf stat full event set, -r5, A/B/C | scripts/collect_perf.py | per-command stderr/stdout/date/status | PASS active-core counters; unused atom limits disclosed |
| supported event fallback, scaling/scope, no invented zeros | collect_perf.py; README and report | raw full/split groups, table generator | PASS active-core counters; unused atom limits disclosed |
| perf record -g and readable reports A/B/C | collect_perf.py | results/perf/report-{A,B,C}.stdout.txt | PASS; zero lost samples, outer-frame limits disclosed |
| IPC and branch % computed comparably | scripts/tables.py | table.json/table.md with source scope notes | PASS |
| Performance analysis **15**: 1–2 page report and full results table | docs/report.md + report.pdf | actual PDF page/layout check | PASS |
| operation winners and reasons, branch and cache behavior | report sections1–3 | actual medians/counters/sampled symbols | PASS |
| page faults/allocation, production choice, next work | report sections4–6 | measured faults; clearly labelled hypotheses and limits | PASS |
| Readability **5**: simple headers, shared infrastructure, viva | README, DESIGN, VIVA; small reproduce scripts | skeptical source/diff review | PASS |
| Student identity and submission directory | README: Rushi Jadhav, SST email,10144; individual | isolated Capstone/10144 only | PASS |
| Reproduce scripts preserve failures, no bulky data/secrets | scripts, local .gitignore | git scope / staged file check | PASS |
| Fresh intended-files build; README commands/links | FINAL_VERIFICATION | clean temp build+verify+smokes and relative link check | PASS |
| Commit, push fork branch, PR to upstream main | authorized fork; branch capstone/10144-order-book | [PR #132](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/132), paginated 145-file scope check, no CI configured | PASS; ready for review |

Counter unavailability is explicitly permitted in the PDF FAQ if working counters
and limitations are reported; it is not permission to fabricate data. Report
readiness is decided from the actual profiling results, recorded separately.
