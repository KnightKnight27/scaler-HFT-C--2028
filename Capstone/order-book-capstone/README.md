# Low-Latency Order Book

**Name:** Isha Patel
**Email:** isha.24bcs10455@sst.scaler.com  
**Roll No:** 10455 

---

A C++17 capstone comparing three order-book data structures for one instrument using the same API and deterministic workload. The project does not implement order matching.

---

## Implementations

| ID | Class | Data Structure |
|----|-------|---------------|
| **A** | `MapOrderBook` | `std::map` — maintains ordered price levels |
| **B** | `VectorOrderBook` | Sorted vectors of price levels |
| **C** | `ArrayOrderBook` | Fixed-size arrays for the supported price range |

---

## Price Model and Workload

- **Tick size:** 0.01
- **Bid range:** 50.00 – 99.99
- **Ask range:** 100.01 – 150.00
- **100.00 is invalid.**
- **Benchmark:** 1,000,000 operations, seed 42, five runs per implementation
- **Operation counts:** 700,471 ADD, 199,303 CANCEL, 100,226 BBO
- 10,000 depth queries are performed every 100 operations. Their cost is included in total runtime but not timed separately.

---

## Project Structure

```text
.
├── include/
│   ├── array_order_book.hpp
│   ├── map_order_book.hpp
│   ├── order_book.hpp
│   └── vector_order_book.hpp
├── src/
│   ├── array_order_book.cpp
│   ├── main.cpp
│   ├── map_order_book.cpp
│   └── vector_order_book.cpp
├── tests/
│   └── correctness_tests.cpp
├── report/
│   ├── benchmark_result.txt
│   └── performance_analysis.md
├── .gitignore
├── CMakeLists.txt
├── README.md
└── bench.cpp
```

---

## Build

Requires a C++17 compiler and CMake 3.16 or newer.

```bash
cmake -S . -B build
cmake --build build --config Release
```

---

## Run

**Run tests:**

```bash
ctest --test-dir build --output-on-failure
```

Run the demo and benchmark using the executable paths produced by your CMake generator. For a Makefiles/Ninja build, the commands are typically:

```bash
./build/demo
./build/bench --impl A
./build/bench --impl B
./build/bench --impl C
```

On Windows, use the generated `.exe` path (for example, `.\bench.exe --impl A` when your shell is in the directory containing `bench.exe`).

Each benchmark implementation uses the same deterministic trace. Compare both timings and correctness outputs; all three implementations reported checksum **508890804** and final BBO values of bid **99.99 × 2267** and ask **100.01 × 2199** in the latest supplied run.

---

## Latest Benchmark Summary

**Environment:** Windows 11, MSYS2 UCRT64, g++ 16.1.0, C++17 `-O2`

| Metric              | A — `std::map` | B — Sorted Vector | C — Fixed Array |
|---------------------|:--------------:|:-----------------:|:---------------:|
| Median total time   | 473 ms         | 414 ms            | **226 ms**      |
| ADD latency         | 429.9 ns/op    | 309.2 ns/op       | **163.0 ns/op** |
| CANCEL latency      | 624.1 ns/op    | 716.6 ns/op       | **346.5 ns/op** |
| BBO latency         | **39.5 ns/op** | 44.9 ns/op        | 40.0 ns/op      |

**Takeaway:** C had the lowest measured total runtime, ADD latency, and CANCEL latency. A had the lowest measured BBO latency; the 0.5 ns/op difference between A and C is very small. Results depend on the machine, compiler, and workload.

---