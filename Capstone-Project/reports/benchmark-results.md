# Benchmark Results

Environment: local workspace machine, C++17, `g++ -O2`, `std::chrono::steady_clock`.

| Metric | A: map | B: lazy heap | C: arrays |
|---|---:|---:|---:|
| Total time, median ms | 493.049 | 2710.26 | 247.157 |
| ns / add, median | 454.580 | 1122.790 | 183.400 |
| ns / cancel, median | 734.435 | 1297.540 | 469.444 |
| ns / BBO, median | 42.3489 | 14019.5 | 33.6499 |
| IPC | unavailable | unavailable | unavailable |
| Branch misses | unavailable | unavailable | unavailable |
| L1 d-cache-load misses | unavailable | unavailable | unavailable |
| Cache misses | unavailable | unavailable | unavailable |
| Page faults | unavailable | unavailable | unavailable |

All three runs produced checksum `4496618155` and final best bid tick `9999` / best ask tick `10001`.

`perf` was unavailable in the current environment (`perf: command not found`), so hardware-counter values were not collected.
