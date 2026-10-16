# Measurement record

Captured 16 October 2026 on the user's Coder Nami EC2/KVM guest: Ubuntu 20.04.6,
Linux 5.15.0-1084-aws, Intel Xeon Platinum 8375C, 64 logical CPUs,
132,921,864,192 bytes guest RAM and 64-byte cache lines. GCC 9.4.0 compiled with
`-std=c++17 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Wpedantic`;
perf 5.15.179. CPU 63, nice 10; VM scheduling/background load uncontrolled.

The approved short-lived admin perf targeted only its child. Project code ran as
UID/GID 1000 via `setpriv --no-new-privs`; perf event scope was user-only. Paranoid
remained 4, without permanent capabilities, sysctl changes or system-wide capture.

## Workload and scopes

Seed 42, 1,000,000 primary events: 700,818 adds, 199,505 live cancels, 99,677 BBOs;
10,000 extra depth pairs. Workload hash `4920528738431414721`, all-design checksum
`7679504001345771705`; final bid 99.99 x 2747 / ask 100.01 x 2018.

Five chrono repetitions use fresh books. `timings.csv` adds only the implementation
column to the original rows. API cells are medians of per-run averages. Total
replay includes depth/checksum work but excludes generation, book construction,
destruction and output. API timing is a separate replay; clock overhead stays
included. Empty-clock intervals A/B/C: 19.5496 / 19.4404 / 19.2868 ns. No warmup;
fixed A/B/C order; A rose from 309.518 to 373.579 ms.

The three `perf-stat-?.txt` files are byte-for-byte original `perf stat -r5`
outputs (aggregate means printed by perf). The table's counter medians instead
come from five additional independent single-replay stat captures per design,
consolidated into `perf-runs.csv`. It adds implementation/run columns and preserves
all original field values. Each row's `event_runtime_ns` and `running_percent`
come from perf. All 135 event running percentages are 100.00. L1/dTLB aliases omit
`:u` in their display names; the verbose attribute dump at the end of
`perf-report.txt` verifies kernel/hypervisor/guest exclusion.

For each implementation, take the median of its five counts for each event.
Compute instructions/cycles and 100*branch-misses/branches separately for each run,
then take medians. Do not divide independently medianed counts to obtain the
reported ratios. Whole-process counts include the privilege-drop helper,
generation and teardown, so their scope is wider than chrono replay.

Recorded command pattern (capture project root is abbreviated as `$PROJECT`):

```sh
sudo -n timeout --signal=INT --kill-after=5s 120s perf stat -r 5 -e task-clock:u,cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u,L1-dcache-load-misses:u,dTLB-load-misses:u,page-faults:u -- setpriv --reuid=1000 --regid=1000 --init-groups --no-new-privs "$PROJECT/build/bench" --impl A --runs 1 --mode total
sudo -n timeout --signal=INT --kill-after=5s 120s perf record -e cpu-clock:u -F 999 --call-graph fp -o perf-A.data -- setpriv --reuid=1000 --regid=1000 --init-groups --no-new-privs "$PROJECT/build/bench" --impl A --runs 9 --mode total
```

Repeat for B/C. Individual stat captures use `-x,` instead of `-r5`. All completed
commands returned 0. Sampling collected 3,414/1,604/981 samples for A/B/C, with zero
reported loss. `perf-report.txt` concatenates the original three self reports and
three inclusive call graphs with labelled separators. Percentages describe whole
recordings; inlining, unresolved libc addresses and incomplete callchains limit
API attribution. No invented function names, tail latency or RSS are reported.

The completed full-depth million-event verification returned 0 after 401.147 s under
a 1,200-second limit. The earlier 300-second timeout and invalid even-ten-replay
recording attempts remain in the original evidence; the submitted results use
the completed check and valid nine-replay profiles. Mac million-event, stress,
Clang and ASan/UBSan logs remain preserved locally; these checks cover the same
unchanged active C++ sources.

## Source and recording provenance

All submitted measured C++/Makefile files match these captured SHA-256 values.
The capture manifest also included an unused historical bitmap header, which was
not included by either active program and is omitted from the submission.

| File | SHA-256 |
|---|---|
| `Makefile` | `ae02cbb1c1093ecd7777dd8d4513779e71e79c76d53074def1587118e3638eeb` |
| `include/orderbook/types.hpp` | `570d0bdf1d4005e17716035e86ae23138128618abfde334ce58140d0b8561e6f` |
| `include/orderbook/tree_book.hpp` | `ff9d49157a82688559494827e8c28bb1f10cdbdc80587cc0815bf9dffa08c5bf` |
| `include/orderbook/vector_book.hpp` | `570d7ccb5bfd8760ef800cd4aa303cffdb064cb2a6b07784cf70e13f376859dd` |
| `include/orderbook/interleaved_book.hpp` | `e5345659d98d7508f76149adcbe2f8d406258c329cc78148c2e660ece00ea3c4` |
| `include/orderbook/workload.hpp` | `b844016f6992bb6d771d8393e6c1a1fcaf572922f1cb788c35ef702e60e8cbe5` |
| `src/bench.cpp` | `b64944f6e008e09a48497c0948917e9824ee3133e9db65aff46d2d021774f0dc` |
| `tests/verify.cpp` | `f4d028c63a1c85d7f620662b0515475122d02079623df4194b45e97aeed70f39` |

Profiled Linux executable SHA-256:
`c7cc7635aeaf07d6deaebad0b05eef6c6ac2ec6e83f338b6db885b8d6b10bee2`.
Original raw recordings remain preserved outside Git, identified below:

| Implementation | Raw bytes | SHA-256 |
|---|---:|---|
| A | 342892 | `b6a1cf7acb88d8e43e37d2f80598ea0098d463dacb1fae17543ac23c8cf58a42` |
| B | 180352 | `6047bc041bd225195707d21704cb118296e0059e105deb81e33f75825094efec` |
| C | 121252 | `08549ed86cff897c0a9f6d2be5c307134bb6a4c73f9abd318137858248ecd99f` |

All Nami originals remain under `/home/coder/orderbook-10222-20261016-mdg_dhky`;
successful raw profiles remain in `/var/tmp/mohit-orderbook-perf.u1I4huWn`.
No Nami files were deleted or workspace stopped. The original complete local
project also retains full captures, metadata and matching ELF. Consolidation
changes file organization only; no measurement values were regenerated or altered.
