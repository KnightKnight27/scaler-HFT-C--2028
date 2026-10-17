# Results

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 392.45 | 507.65 | **305.18** |
| ns / add | 324.64 | 362.50 | **317.95** |
| ns / cancel | 619.28 | 1,069.21 | **214.44** |
| ns / BBO | **75.65** | 90.74 | 77.79 |
| Task clock (ms) | 2,124.65 | 3,080.91 | **1,670.58** |
| Cycles | 4,447,005,329 | 6,161,049,052 | **3,186,740,084** |
| Instructions | 3,987,425,388 | 5,255,829,528 | **3,337,106,365** |
| IPC | 0.897 | 0.853 | **1.047** |
| Branches | 806,189,984 | 1,109,523,654 | **545,806,105** |
| Branch misses | 40,215,480 | 29,866,229 | **12,687,482** |
| Branch misses (%) | 4.99% | 2.69% | **2.32%** |
| L1-dcache-load-misses | 63,330,895 | 85,259,642 | **24,307,276** |
| Cache misses | **26,279,035** | 37,813,160 | 34,026,954 |
| dTLB-load-misses | 2,960,510 | 5,176,851 | **2,363,691** |
| Page faults | **16,358** | 16,983 | 88,682 |

Hardware rows use the `cpu_core` counters, which covered 98.09–99.34% of each run. IPC and branch-miss percentages are calculated from those counters. The `cpu_atom` L1 data-cache event was not supported.

Final checksum for all three implementations: `10658993729413417588`.
