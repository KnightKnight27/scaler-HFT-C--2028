# Low-latency order book (C++17)

**Name- Arman Barbhuiya** 

**Roll Number- 24BCS10196**

**Email- arman.24bcs10196@sst.scaler.com** 



---

### Three single-threaded order-book designs compared through one benchmark.
---
## Scope and Assumptions

 - One instrument; no matching engine or trade generation. Orders rest until cancelled.
 - Price tick size is 0.01. The implementation represents prices as integer ticks: for example, 99.99 is 9999 and 100.01 is 10001.
 - Bid ticks: 5000..9999; ask ticks: 10001..15000. Tick 10000 is intentionally unused.
 - Quantities on individual orders are 1..100
 - The program is single-threaded and uses only the C++ standard library.
---
## Deviations from the Specification
 - Replaced double prices with uint16_t ticks, since prices up to 15,000 fit within 65,535.
 - Used uint8_t for individual order quantities, since the maximum quantity is 100.
 - Reduced order IDs from uint64_t to uint32_t, as generated IDs fit within 32 bits
 - Used uint32_t for aggregate quantities to prevent overflow when combining multiple orders at a price level.
 - Removed const from get_bbo(), get_bids(), and get_asks() because Implementation B temporarily modifies internal data structures during these operations.
 - Added combined timing measurements for get_bids() and get_asks() as ns / Snapshop, as the specification requires them to be called every 100 operations.
- Added cancellation attempts for already cancelled orders to test handling of repeated cancellations.


## Build-

    g++ -O2 bench/bench.cpp src/OrderbookOne.cpp src/OrderbookTwo.cpp src/OrderbookThree.cpp -o bench

    g++ -O2 test/test.cpp src/OrderbookOne.cpp src/OrderbookTwo.cpp src/OrderbookThree.cpp -o verify
   
  ## Correctness verification
Run-

    ./verify

Expected output-

    Finished implementation A
    Finished implementation B
    Finished implementation C
    
    ========== VERIFICATION ==========
    A == B: PASS
    A == C: PASS
    B == C: PASS
    All implementations match!

## Benchmark
Run one implementation per process-

    ./bench --impl A
    ./bench --impl B
    ./bench --impl C
We performs five trials and print the median total execution time and median average time for add, cancel, BBO, and bid/ask depth queries. It uses std::chrono::steady_clock, a seeded std::mt19937 for generated values, and a separate seeded generator for buy/sell side selection. Each run uses the same generator setup, while generator state continues across the five trials within that run. A checksum consumes query output so that the compiler doesnt optimize it away.
 
 ---
## Linux profiling
Select any one out of <A|B|C> while running-

       taskset -c 0 perf stat \
        -x, \
        -o "./perf_results/<A|B|C>.csv" \
        -r 1 \
        -e task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults \
        -- ./output/b --impl <A|B|C>



---
## Metrics
|                       | Implementation A | Implementation B | Implementation C |
|-----------------------|-----------------:|-----------------:|-----------------:|
| Total time (ms)*      | 157.508          | 67.859           | 67.496           |
| ns / add              | 148.942          | 29.970           | 36.646           |
| ns / cancel           | 130.985          | 82.129           | 84.467           |
| ns / BBO              | 18.518           | 17.723           | 15.059           |
| ns / Snapshot         | 178.006          | 664.61           | 194.74           | 
| IPC                   | 1.019            | 2.099            | 1.871            |
| Branch misses (%)     | 5.236%           | 0.254%           | 0.797%           |
| L1-dcache-load-misses | 41,629,285       | 6,718,482        | 8,018,951        |
| Cache misses          | 2,367,367        | 514,195          | 1,132,128        |
| Page faults           | 48,602           | 27,540           | 34,351           |

----
## Implementation A
This is a deliberately simple and slow implementation to develop a baseline for both correctness and timings, it uses 2 std::map to store price to quantity mapping, and a std::unordered_map to store order id to map::iterator mapping. 
Best bid/ask is supported by map, with begin() and rbegin() operation.
Iterator of the map element is found using the umap for cancellation.

Big-O complexity:
using-  n as number of active orders, d as depth of get_bids/get_asks query
add_order - O(log(n))
cancel_order- average O(1)
get_bbo- O(1)
get_bids- O(d log(n))
get_asks- O(d log(n))

Certain parts use branhless techniques and all data structure in this implementation use pmr::monotonic_buffer_resource. Reasoning behind both is explained in Low Latency optimizations section of this document.


---
## Implementation B
This implementation uses 2 heaps (std::priority_queue) to store prices.
It uses a std::vector of fixed size of Range of the buy and sell prices to store currently available quantities for each price level.
It also uses a std::unordered_map to store order id to price and quantity mapping to aid in cancelling orders.
For best bid/ask, we use our .top() operation of heaps. For every best bid/ask we get we check in the vector to ensure we have more than 0 quantity available, we keeping poping from the heap until such a price is found.
For cancellation, we retrieve the price and quantity of the cancelled order from our umap, then subtract the quantity from that price's slot in the vector.

Big-O complexity:
using-  n as number of active orders, d as depth of get_bids/get_asks query, p as total number of ordered, k as numner of stale orders in heap
add_order - O(log(n))
cancel_order- average O(1)
get_bbo- O(k log(p))
get_bids- O(p log(p))
get_asks- O(p log(p))

Certain parts use branhless techniques and the umap in this implementation uses pmr::monotonic_buffer_resource. Reasoning behind both is explained in Low Latency optimizations section of this document.

---

## Implementation C
This is a deliberately complex implementation, would ve been good to say complex and fast, but it doesnt beat Implementation B.

It is similar to implementation B in the sense that, we still use a vector of fixed size to store the current quantities, and a umap to store order id to price and quantity map, but rather than using heap to retrieve best bid/ask, we basically iterate in an array to find the best currently active prices, with a tiny optimization-
So, for every price, which in this case is in range of [5000,15000] all we need to store is a bit to mark that price as active, so we are making a vector of ceil(range/64) uint64_t elements, each 0-5000 bit of that vector would act as a bool for the price.
To add an order we update the quantity in the quantities array, set the bit that represents this parrticular price as true, and add a mapping to the umap
To find the best bid/ask prices, we iterate on the vector to find the best uint64_t which !=0, ie a bit of the uint64_t represents an order, which is active. Then we just kind of binary search on the parts of the the uint64_t until we are left with 1 byte ie 8 bits, since memory is byte addressable, we cant continue our binary search structure, and have to iterate and use bit man to find the exact best bit which is set.
To cancel an order, we use the mapping from umap to get price and quantity, we subtract the quantity in the quantities array, and if in the quantities array the quantity reach 0, then we unset the bit which represents this particular price.
We also cache BBO in this implementation, since this is the only implementation which doesnt have  O(1) get_bbo(), we invalidate our cache if a better price gets added or if our cached price gets cancelled



Big-O complexity:
using-  p as total price ticks
add_order - O(1)
cancel_order- average O(1)
get_bbo- O(1) if cache hit, O(p/64) otherwise
get_bids- O(p log(p))
get_asks- O(p log(p))

Certain parts use branhless techniques and the umap in this implementation uses pmr::monotonic_buffer_resource. Reasoning behind both is explained in Low Latency optimizations section of this document.

---

## Major Low Latency Optimizations

### 1. `std::pmr::monotonic_buffer_resource`
The idea behind using std::pmr::monotonic_buffer_resource is to improve allocation performance and potentially improve cache locality for data structures that perform frequent dynamic allocations, such as std::map and std::unordered_map used in all the implementations.

It offers two potential benefits:

1.  **Faster allocations:** Rather than managing individual allocations and deallocations, a monotonic buffer resource allocates memory from its current buffer, typically advancing a pointer to obtain the next available space. When the buffer runs out, it obtains another buffer. Individual deallocations do not reclaim memory for reuse; the allocated memory is released together when the resource is destroyed.
    
2.  **Potentially better cache locality:** Since allocations are typically served sequentially from the same buffer, nodes allocated close together in time may also occupy nearby memory addresses. For node-based data structures, this improve spatial locality and reduce cache misses when accessing related nodes. An example of the same can be seen in ./perf_results/perf_A.csv
    


### 2. Branchless Techniques

Branchless coding aims to avoid conditional branches in performance critical sections of code by replacing them with operations such as conditional moves, arithmetic or indexed lookups.
A conditional branch can be expensive when the CPU predicts its outcome incorrectly. Modern CPUs use branch predictors to speculate about which path a program will take, allowing execution to continue before the branch condition is fully resolved. When the prediction is wrong, the CPU must discard incorrectly executed speculative work and redirect execution, wasting cycles.

This is particularly relevant to this order book because buy/sell side selection is driven by a separate seeded RNG.  So a branch that handles buys and sells separately can incur frequent branch mispredictions.

For example, instead of:

```
size_t bit_position;

if (side == Side::Buy) {
    bit_position = price - ask_offset;
} else {
    bit_position = price - bid_offset;
}
```

we have expressed the selection as:

```
u16  branch_arr[2];
branch_arr[0] =  price-ask_offset;
branch_arr[1] =  price-bid_offset;
size_t  bit_position  =  branch_arr[side==Side::Buy];
```

The boolean comparison evaluates to `0` or `1`, selecting the corresponding array element without explicitly writing an `if` statement.

This gives the compiler an opportunity to generate code without a conditional branch.

---
## Performance Analysis and Design Trade-offs.
### Branch Prediction - 
Implementation A, has the highest branch mispredictions. This must be because implementation A uses map, which uses rbt, and searching in structure like rbt is prone to a lot of branch misprediction due to less predictability. Implementation C also has a higher than ideal branch misprediction rate, most of it (~50%) comes from the add_order() function, but only branching in it (atleast in cpp code), is to check validity of BBO cache, and i wasnt able to figure out a way to remove that in the limited time I had. 

### Cache Behavior -
Implementation A also has the highest cache misses, which makes sense since all the data structures used in it are Node based data structures, which are prone to have cache miss due they their nodes being spread out in memery, the cache miss numbers were reduces by 30-50% across all implementations by using std::pmr::monotonic_buffer_resource.

### Production Choice - 
With the current benchmarks and assuming we dont have to call snapshot operations per 100 operations in prod, implementation B seems to be the clear choice. Implementation C is a close second, if a way can be found to reduce branch mispredictions, i belive it would get really close to if not beat implementation B on all benchmarks


---
## -   Future Work
Since all three implementations use Hash map (std::unordered_map), replacing them with a flat hash map (aka Swiss Table) as discussed in the following CppCon could be a potential optimization worth investigating.
https://www.youtube.com/watch?v=ncHmEUmJZf4