//THIS FILE IS ANNOTATED USING AI, CODE IS WRITTEN BY ME

#pragma once

#include <cstdint>
#include <cmath>
#include <vector>

// 1. Required API Types from Section 3 of the Capstone Spec
enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;
};

// 2. Price <-> Tick Conversion Helpers (Chapter 14 & Hint 3)
// Bid ticks: [50.00, 99.99]  -> [5000, 9999]
// Ask ticks: [100.01, 150.00] -> [10001, 15000]
constexpr int32_t MIN_BID_TICK = 5000;
constexpr int32_t MAX_BID_TICK = 9999;
constexpr int32_t MIN_ASK_TICK = 10001;
constexpr int32_t MAX_ASK_TICK = 15000;
constexpr int32_t TICKS_PER_SIDE = 5000;

int32_t price_to_tick(double price) {
    // Adding 0.5 before casting (or using std::llround) prevents binary float 
    // representation quirks like 99.97999999999999 from truncating to 9997.
    return static_cast<int32_t>(std::llround(price * 100.0));
}

double tick_to_price(int32_t tick) {
    return static_cast<double>(tick) * 0.01;
}

// 3. Workload Event Struct for Correctness Check & Benchmark (Sections 5 & 6)
enum class OpType : uint8_t {
    Add,
    Cancel,
    GetBBO
};

struct Operation {
    OpType type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

// Internal order state needed across implementations to process cancellations
struct StoredOrder {
    Side side;
    int32_t tick;
    long qty;
};