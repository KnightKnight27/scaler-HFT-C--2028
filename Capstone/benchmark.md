# Order Book Benchmark Report



## Benchmark results

| Metric | A (`std::map`) | B (priority queue) | C (sorted list) |
|---|---:|---:|---:|
| Median total time | 17.16 ms | 11.46 ms | 6,952.50 ms |
| Average add latency | 161.5 ns | 89.0 ns | 99,065.4 ns |
| Average cancel latency | 185.1 ns | 91.5 ns | 215.5 ns |
| Average BBO latency | 20.8 ns | 22.5 ns | 81.2 ns |
| Median page faults | 1 | 177 | 276 |
| Max RSS | 10,544 KB | 10,108 KB | 12,808 KB |

<details>
<summary>Benchmark output screenshots for B and C</summary>

![Five-run benchmark results for Implementation B](screenshots/image%20copy%203.png)

![Five-run benchmark results for Implementation C](screenshots/image%20copy%202.png)

</details>

## `perf stat` results

Collected with five external repetitions of each command using `--runs 1`. Core columns below use the `cpu_core` events; hybrid CPU atom counts are omitted. IPC and branch-miss percentage are calculated from the recorded core counts. The requested L1 data-cache and dTLB events were not included in these `perf stat` commands.

| Metric (`cpu_core`, unless noted) | A | B | C |
|---|---:|---:|---:|
| Core cycles | 84,662,698 | 66,784,499 | 25,623,011,762 |
| Core instructions | 108,248,970 | 122,066,701 | 2,781,841,522 |
| IPC | 1.28 | 1.83 | 0.11 |
| Core branches | 20,192,064 | 21,067,923 | 908,650,571 |
| Core branch misses | 953,184 | 335,489 | 296,263 |
| Branch-miss rate | 4.72% | 1.59% | 0.03% |
| Core cache misses | 94,932 | 115,218 | 20,278,844 |
| Page faults | 2,025 | 2,070 | 2,655 |
| `perf` elapsed time | 26.59 ms | 23.02 ms | 6.84 s |

The `perf` elapsed time includes process and workload-generation overhead and is separate from the benchmark's internally measured median execution time. Do not compare it directly to the median total-time row.

<details>
<summary>Hardware counter output screenshots</summary>

![perf stat results for Implementation A](screenshots/perf%20A/2.png)

![perf stat results for Implementation B](screenshots/perf%20B/3.png)

![perf stat results for Implementation C](screenshots/perf%20C/3.png)

</details>

## Analysis

B had the shortest benchmark runtime and lowest measured add and cancel averages. Its hash-map lookups and lazy deletion avoid immediate heap maintenance during cancellation. A had the lowest average BBO latency, narrowly ahead of B. C's sorted-list insertion scans existing orders, explaining its much higher add latency and overall runtime.

The `perf` core counters show B with the highest IPC and lowest branch-miss rate among the three. C had by far the most core cache misses and a much lower IPC, consistent with the poor locality and repeated traversal associated with a linked list. Its low branch-miss percentage does not mean it is faster: branch-miss percentage alone does not account for the large volume of work or cache stalls.

## Reproduction

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bench -j"$(nproc)"

./build/bench --verify --ops 100000 --seed 42
./build/bench --impl A --ops 100000 --runs 5 --seed 42
./build/bench --impl B --ops 100000 --runs 5 --seed 42
./build/bench --impl C --ops 100000 --runs 5 --seed 42

perf stat -r 5 -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,page-faults \
  -- ./build/bench --impl A --ops 100000 --runs 1 --seed 42
```

For B and C, repeat the `perf stat` command with `--impl B` and `--impl C`, respectively.
