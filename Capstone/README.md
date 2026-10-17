# Capstone Project: Low-Latency Order Book

**Name:** Apurv Dugar  
**Roll Number:** 24BCS10107  
**Email:** apurv.24bcs10107@sst.scaler.com  

---

## Project

Three implementations of a single instrument order book using three different data structures and benchmarking them to make a low latency order book. 

## Architecture & My Data Structure Choices

### Implementation A: std::map [a Red–Black Tree]
    
- Used the standard std::map of C++ which is a balanced binary search tree as it keeps the prices sorted descending order making the BBO lookups at the begin of the tree.
- Implemented with price as keys and quantity as values representing the total quantity at that price level. 
- Trade off - The entire structure lies on the heap memory and there will be too many pointer chasing involved when orders are added and cancelled.

Time Complexities: 
- add_order() - O(logN)
- cancel_order() - O(logN)
- get_bbo() - O(1)
- get_bids() - O(N)
- get_asks() - O(N)

### Implementation B: priority_queue with lazy deletion

- Used a priority queue approach as we need very fast BBO api, and pq.top() gives us the best bid and ask in O(1) time complexity. 

- Trade off - The priority queue doesn't allow arbitrary deletion of elements. So, when an order is cancelled I mark it as deleted in the orders map but remove it from the pq only when BBO is called or get_bids/get_asks is called.

Time Complexities: 
- add_order() - O(logN)
- cancel_order() - O(logN)
- get_bbo() - O(k log N)
- get_bids() - O(k log N)
- get_asks() - O(k log N) 
where k is the number of cancelled orders at the top of the heap

### Implementation C: a fixed size array indexed by price levels
    
- Since there are 10,000 price levels (5,000 for bids and 5,000 for asks), instead of dynamically allocating them into memory, I used two 1D arrays of size 5,000 (`long bids[5000]` and `long asks[5000]`) and mapped the prices directly to array indices via offset arithmetic. The arrays store the aggregate quantity at each price level. 
- Trade off - When the best price level is cancelled out completely then we have to linear search to find the next best price level.  

Time Complexities: 
- add_order() - O(1)
- cancel_order() - O(1)
- get_bbo() - O(1) if the best price level is not cancelled out, else O(k) where k is the number of cancelled orders at the top. 
- get_bids() - O(N)
- get_asks() - O(N)

---

## How to Build 
Compile with `-O2` optimization as specified:
```bash
g++ -O2 bench.cpp -o bench
```

## How to Run the Correctness Test
Run the verification mode that executes 100,000 identical operations through all three implementations and asserts that their BBO, quantities, bid/ask depth levels, and cancel results match completely:
```bash
./bench --verify
```
```
RUNNING CORRECTNESS CHECK

Implementation A (Map):   PASS
Implementation B (PQ):    PASS
Implementation C (Array): PASS
All implementations produce identical results.
```
Output added to `verify_output.txt`

## How to Run Benchmark for each implementation
Run 1,000,000 operations (seed 42, median of 5 runs):
```bash
./bench --impl A    # std::map
./bench --impl B    # priority_queue
./bench --impl C    # Fixed Array
```

### Profile with Linux `perf`

perf stat: 
```bash
perf stat -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl A 2> perf_stat_A.txt

perf stat -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl B 2> perf_stat_B.txt

perf stat -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults ./bench --impl C 2> perf_stat_C.txt
```

perf report:

```bash
perf record -g -o perf_A.data ./bench --impl A
perf report -i perf_A.data --stdio > perf_report_A.txt

perf record -g -o perf_B.data ./bench --impl B
perf report -i perf_B.data --stdio > perf_report_B.txt

perf record -g -o perf_C.data ./bench --impl C
perf report -i perf_C.data --stdio > perf_report_C.txt
```

## Interface Changes

Changed the interface and using integer (uint32_t) instead of double for prices in Order and BBO struct definitions as equality checks for floating point data type is not reliable and may lead to incorrect results.

```cpp
struct Order {
    uint64_t id;    
    Side side;       
    uint32_t price;  // uint32_t instead of double
    long qty;        
};     

struct BBO {
    bool has_bid;
    uint32_t bid_price; // uint32_t instead of double
    long bid_qty;

    bool has_ask;
    uint32_t ask_price; // uint32_t instead of double
    long ask_qty;
};
```
---

## Results

| Metric | Implementation A (`std::map`) | Implementation B (`priority_queue`) | Implementation C (Fixed Array) |
| :--- | :--- | :--- | :--- |
| **Total time (ms)** | 246.90 ms | 587.31 ms | 116.63 ms|
| **ns / add** | 222.56 ns | 139.83 ns | 80.56 ns|
| **ns / cancel** | 343.72 ns | 252.48 ns | 192.26 ns|
| **ns / BBO** | 15.28 ns | 40.33 ns | 15.43 ns|
| **IPC (Instructions / Cycle)** | 0.77 | 1.58 | 1.23 |
| **Branch misses (%)** | 5.62% | 3.28% | 1.42% |
| **L1-dcache-load-misses** | 72,095,131 (72.10 M) | 157,265,872 (157.27 M) | 27,911,605 (27.91 M) |
| **Cache misses (LLC)** | 28,514,101 (28.51 M) | 30,451,589 (30.45 M) | 22,592,790 (22.59 M) |
| **Page faults** | 22,642 | 62,543 | 22,503 |

---

## Project Structure

```
.
├── types.hpp              # Shared definitions: Side, Order, Level, BBO
├── MapOrderbook.hpp       # Implementation A: std::map (RB-tree)
├── PQOrderbook.hpp        # Implementation B: Priority queue with lazy deletion
├── ArrayOrderbook.hpp     # Implementation C: Direct-indexed flat array
├── bench.cpp              # Unified benchmark & correctness verification harness
├── benchmarking.txt       # Raw benchmark output (5 individual runs & median for A, B, C)
├── verify_output.txt      # 100,000-operation lockstep correctness verification log
├── perf_stat_A.txt        # perf stat hardware performance counters for Impl A
├── perf_stat_B.txt        # perf stat hardware performance counters for Impl B
├── perf_stat_C.txt        # perf stat hardware performance counters for Impl C
├── perf_report_A.txt      # perf report callgraph disassembly for Impl A
├── perf_report_B.txt      # perf report callgraph disassembly for Impl B
├── perf_report_C.txt      # perf report callgraph disassembly for Impl C
├── README.md              
└── REPORT.pdf             

```

---
