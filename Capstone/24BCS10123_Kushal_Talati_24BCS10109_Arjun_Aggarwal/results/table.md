Linux (ubuntu docker container), median of 5 runs, 1,000,000 ops, seed 42.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 288.6 | 196.6 | 83.9 |
| ns / add | 204.5 | 109.0 | 40.6 |
| ns / cancel | 562.7 | 370.4 | 113.5 |
| ns / BBO | 42.7 | 43.8 | 46.1 |
| Instructions | 1,383,142,808 | 1,470,388,546 | 532,333,542 |
| Branches | 200,390,688 | 182,510,357 | 49,878,144 |
| Branch misses | 18,057,619 | 5,280,482 | 2,582,745 |
| Cycles | 3,104,932,500 | 1,577,191,500 | 315,414,000 |
| IPC | 0.45 | 0.93 | 1.69 |
| Branch misses (%) | 9.0 | 2.9 | 5.2 |
| L1-dcache-load-misses | 18,891,616 | 15,185,537 | 2,211,739 |
| Cache misses | 4,446,345 | 4,392,404 | 1,670,976 |
| dTLB-load-misses | 4,000,000 | 2,000,000 | 200,000 |
| Page faults | 8,156 | 7,925 | 2,227 |


Same benchmark natively on the mac (apple clang), median of 5. no perf on macOS.

| Metric | Implementation A | Implementation B | Implementation C |
|---|---:|---:|---:|
| Total time (ms) | 232.8 | 152.0 | 54.4 |
| ns / add | 159.5 | 75.4 | 24.2 |
| ns / cancel | 461.8 | 264.5 | 53.9 |
| ns / BBO | 22.9 | 37.0 | 20.6 |
