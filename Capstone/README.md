# Low-Latency Order Book

**SST email:** bandari.24bcs10359@sst.scaler.com and yuvraj.24bcs10405@sst.scaler.com  
**Roll number:** 24BCS10359(Sai Niketh.B) and 24BCS10405(yuvraj Lamba)

Single-instrument, single-threaded resting order book exercise implemented in
C++17. There is no matching engine or trade generation.


## Build

With g++:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic order_book.cpp ordered_map_order_book.cpp sorted_vector_order_book.cpp fixed_array_order_book.cpp bench.cpp -o bench
```

Or configure/build with CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

For a reproducible native run on Linux:

```sh
sudo apt update
sudo apt install build-essential linux-tools-common linux-tools-$(uname -r)
chmod +x run_all.sh
./run_all.sh
```

The script builds with C++17, runs correctness verification, benchmarks
implementations A/B/C, and collects Linux `perf` counters and call-graph
reports. The complete combined perf output is written to
`results/perf_results.txt`; per-implementation files are also written under
`results/`.

On Windows PowerShell, use `.\run_all.ps1`. On macOS, the script uses Apple's
Xcode Instruments CPU Counters template instead of Linux `perf`.

## Run

```sh
./bench --verify
./bench --impl A
./bench --impl B
./bench --impl C
```

`--verify` generates one seeded 1,000,000-operation workload, tests the
empty-book, unknown/already-cancelled ID, and insufficient-depth requirements,
then compares BBO, top-10 bid levels, top-10 ask levels, and cancellation
results from all three books after every operation.

Each `--impl` run uses the same seeded workload and runs it five times. It
prints every run's total time and average/p99 latency for add, cancel, and BBO
queries, followed by the median of each metric, final BBO, and checksum. The
reported p99 is the nearest-rank 99th percentile from per-operation samples.
An empty timer-interval calibration is printed for context; a reported 0 ns
can result from clock resolution and does not mean the clock calls have no
overhead. Operation latencies include the timer calls and storing latency
samples. The workload is prepared before timing, cancels always target live
orders, and depth-10 bid/ask reads are performed every 100 operations. Total
time includes the per-operation timing and depth reads but excludes workload
generation.

## Interface and price representation

The implementation uses integer cents (`PriceTicks`) in its shared interface
instead of `double`. For example, `9999` means 99.99. This avoids floating-point
price comparisons. The valid buy ticks are 5000–9999 and valid sell ticks are
10001–15000; 10000 (100.00) is intentionally not a valid order price.
Quantities must be positive. Order IDs are required to be unique and never
reused, as specified by the assignment. The ID index retains cancelled-ID
tombstones to enforce this rule; unknown and previously cancelled IDs return
`false`. Re-adding any prior ID throws `std::invalid_argument`.

`price_to_ticks` and `ticks_to_price` are provided for conversion at an
external interface. `get_bids` and `get_asks` return available levels in
price-priority order, up to the requested depth.

## Designs and complexity

Let `H` be the number of IDs ever submitted, `L` the active price levels on one
side, and `P` the number of possible ticks on that side (5,000). Hash table
complexities below are average-case; worst-case hash operations are linear.
ID-index memory is O(H) because cancelled IDs must remain reserved.

| Operation | A: ordered maps | B: sorted vectors | C: fixed tick arrays |
|---|---:|---:|---:|
| Add | O(log L) | O(L) | O(1) average |
| Cancel | O(log L) | O(L) | O(P) worst case, O(1) average |
| BBO | O(1) | O(1) | O(1) |
| Get bids/asks, depth `d` | O(d) | O(d) | O(P) worst case |

- **A — ordered map** is implemented in `ordered_map_order_book.hpp/.cpp`. It
  stores each side's aggregate quantities in an ordered `std::map` (bids
  descending, asks ascending). An `std::unordered_map` maps every order ID to
  its side, tick, quantity, and live state. The first map element is the BBO.
- **B — sorted vector** is implemented in `sorted_vector_order_book.hpp/.cpp`.
  It stores each side's aggregate levels in a sorted `std::vector`, with the
  best level first, and uses an `std::unordered_map` for order-ID lookup and
  cancelled-ID tombstones.
  Binary search finds a level; insertion/erasure shifts vector entries.
- **C — fixed array** is implemented in `fixed_array_order_book.hpp/.cpp`.
  It stores quantities in fixed arrays indexed by price tick and keeps the
  best active tick for each side. Order details are in an
  `std::unordered_map`, including cancelled-ID tombstones. Add and BBO are
  constant-time; cancelling the last order at the current best tick scans for
  the next active price.

## Linux perf

On Linux, run the complete workflow with:

```sh
chmod +x run_all.sh
./run_all.sh
```

To run the commands manually after building:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  order_book.cpp ordered_map_order_book.cpp sorted_vector_order_book.cpp \
  fixed_array_order_book.cpp bench.cpp -o build/bench
./build/bench --verify
EVENTS="task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults"
perf stat -r 5 -e "$EVENTS" -- ./build/bench --impl A
perf record -q -g -o results/perf_A.data -- ./build/bench --impl A
perf report --stdio --no-children -i results/perf_A.data
```

`./run_all.sh` executes those commands for A, B, and C and combines the
complete `perf stat` and `perf report` output in
`results/perf_results.txt`. It also saves `perf_stat_A.txt` through
`perf_stat_C.txt`, `perf_report_A.txt` through `perf_report_C.txt`, and
`perf_run_*.txt`. `perf.data` files are kept locally and ignored by Git.
Counter availability depends on the Linux kernel, CPU, and permissions; the
script records failures rather than substituting estimates.

For hardware-counter measurements, use the untimed mode:

```sh
./build/bench --impl none --perf
./build/bench --impl A --perf
./build/bench --impl B --perf
./build/bench --impl C --perf
```

`none` measures workload generation only. Subtract its counter values from
A, B, and C when comparing implementation work. This mode omits per-operation
timers and runs the one-million-operation workload once. On Apple Silicon,
Docker cannot expose hardware counters. To collect Debian `perf` and
Cachegrind results on the same Mac:

```sh
# Start Docker Desktop first, then run:
docker run --rm -it -v "$PWD:/src" -w /src debian:bookworm bash
apt-get update
apt-get install -y build-essential linux-perf valgrind
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer \
  order_book.cpp ordered_map_order_book.cpp sorted_vector_order_book.cpp \
  fixed_array_order_book.cpp bench.cpp -o build/bench-linux
./build/bench-linux --impl none --perf
perf stat -e task-clock,page-faults -- ./build/bench-linux --impl A --perf
valgrind --tool=cachegrind ./build/bench-linux --impl A --perf
```

Repeat the final two commands for B and C. The combined automated output is
written to `results/perf_results.txt`.
If Docker reports that `/Users/.../.docker/run/docker.sock` is missing, start
Docker Desktop and rerun the command.

## Results and report

`results/bench_A.txt`, `bench_B.txt`, and `bench_C.txt` contain the five
individual benchmark runs and median for the machine that ran the script.
`results/verify.txt` contains the correctness run. See
[PERFORMANCE_REPORT.md](./results/PERFORMANCE_REPORT.md) for the measured comparison and limitations.
