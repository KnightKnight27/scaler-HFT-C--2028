#pragma once
#include <cstdint>

// things shared by all three order books
//
// sir said in class to never keep prices as double
// doubles can not hold 0.01 steps exactly and float math is slower than int math
// so every price is stored as an integer number of ticks
// tick size is 0.01 so 100.01 becomes 10001 and 99.98 becomes 9998

enum class Side { Buy, Sell };

// price range from the doc
const int32_t kMinBidTick = 5000;   // 50.00
const int32_t kMaxBidTick = 9999;   // 99.99
const int32_t kMidTick = 10000;     // 100.00, no order can sit here
const int32_t kMinAskTick = 10001;  // 100.01
const int32_t kMaxAskTick = 15000;  // 150.00

// number of slots from 50.00 to 150.00 (the mid slot is always empty)
const int32_t kNumSlots = kMaxAskTick - kMinBidTick + 1;

struct Level {
    int32_t tick;
    long qty;
};

struct BBO {
    bool has_bid;
    int32_t bid_tick;
    long bid_qty;

    bool has_ask;
    int32_t ask_tick;
    long ask_qty;
};

// one resting order
// fields are ordered from biggest to smallest so there is no padding in the middle
// long (8) + int32_t (4) + Side (4) = 16 bytes
// if Side was first we would get 4 + 4 padding + 8 + 4 + 4 padding = 24 bytes
struct Order {
    long qty;
    int32_t tick;
    Side side;
};

// the book only takes valid orders
// all three books call this so they all ignore the same bad input
inline bool valid_order(Side side, int32_t tick, long qty) {
    if (qty <= 0) {
        return false;
    }
    if (side == Side::Buy) {
        return tick >= kMinBidTick && tick <= kMaxBidTick;
    }
    return tick >= kMinAskTick && tick <= kMaxAskTick;
}

inline bool same_bbo(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_tick == b.bid_tick && a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask && a.ask_tick == b.ask_tick && a.ask_qty == b.ask_qty;
}
