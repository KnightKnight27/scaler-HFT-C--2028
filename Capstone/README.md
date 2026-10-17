# Capstone Project

## Team Members

- Mehul Agarwal
    - email: mehul.24bcs10128@sst.scaler.com
    - roll no: 10128
- Sanjay Desai
    - email: sanjay.24bcs10147@sst.scaler.com
    - roll no: 10147


## How to build the Project

```bash
g++ -std=c++17 -O2 -o bench bench.cpp
```

## How to run the correctness test

```bash
./bench --verify
```

## How to run each benchmark

- Implementation A

    ```bash
    ./bench --impl A
    ```
- Implementation B

    ```bash
    ./bench --impl B
    ```
- Implementation C

    ```bash
    ./bench --impl C
    ```

## Interface changes

We didn't make any changes to the interface provided by the problem.

## Data Structure of Choices

- **Implementation A (std::map)**
  - **Bids & Asks**: `std::map` + Doubly Linked List for FIFO orders
  - **Order Lookup**: `std::vector` for $O(1)$ cancel lookup
  - **Memory**: Pre-allocated object pool

- **Implementation B (std::priority_queue)**
  - **Bids & Asks**: `std::priority_queue` (heap)
  - **Order Lookup & Cancel**: `std::vector` with lazy cancellation
  - **Quantities**: `std::unordered_map` for price level totals

- **Implementation C (Direct Index Array)**
  - **Bids & Asks**: Direct-indexed `std::array` + Doubly Linked List for FIFO orders
  - **Order Lookup**: `std::vector` for $O(1)$ cancel lookup
  - **Memory**: Pre-allocated object pool