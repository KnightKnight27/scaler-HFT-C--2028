# perf stat summary

Source: `results/wsl/perf_stat_*.txt` (means over the perf stat -r repeats).
baseline = `bench --generate-only` (process start + workload generation, no book).
n/s = not supported on this machine; - = not present in the file.

| Counter               | baseline | A      | B      | C      |
|-----------------------|----------|--------|--------|--------|
| task-clock (ms)       | 68.46    | 239.95 | 295.08 | 112.73 |
| cycles                | n/s      | n/s    | n/s    | n/s    |
| instructions          | n/s      | n/s    | n/s    | n/s    |
| branches              | n/s      | n/s    | n/s    | n/s    |
| branch-misses         | n/s      | n/s    | n/s    | n/s    |
| cache-references      | n/s      | n/s    | n/s    | n/s    |
| cache-misses          | n/s      | n/s    | n/s    | n/s    |
| L1-dcache-load-misses | n/s      | n/s    | n/s    | n/s    |
| dTLB-load-misses      | n/s      | n/s    | n/s    | n/s    |
| page-faults           | 8932     | 16356  | 16823  | 17166  |

| Derived       | Formula                               | baseline | A   | B   | C   |
|---------------|---------------------------------------|----------|-----|-----|-----|
| IPC           | instructions / cycles                 | n/a      | n/a | n/a | n/a |
| Branch-miss % | branch-misses / branches × 100        | n/a      | n/a | n/a | n/a |
| Cache-miss %  | cache-misses / cache-references × 100 | n/a      | n/a | n/a | n/a |
