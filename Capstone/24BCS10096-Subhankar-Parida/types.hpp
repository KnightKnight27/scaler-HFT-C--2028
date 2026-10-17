#pragma once

#include <cstdint>
#include <cmath>
#include <vector>

// ---- Shared types and constants for all three order book implementations ----

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid = false;
    double bid_price = 0.0;
    long bid_qty = 0;

    bool has_ask = false;
    double ask_price = 0.0;
    long ask_qty = 0;
};

// Tick arithmetic: price 50.00 = tick 5000, price 150.00 = tick 15000
// tick size is 0.01
static constexpr int MIN_BID = 5000;   // 50.00
static constexpr int MAX_BID = 9999;   // 99.99
static constexpr int MID     = 10000;  // 100.00
static constexpr int MIN_ASK = 10001;  // 100.01
static constexpr int MAX_ASK = 15000;  // 150.00

inline int toTick(double price) {
    return static_cast<int>(std::round(price * 100.0));
}

inline double toPrice(int tick) {
    return tick / 100.0;
}

// Validate that the order is within the correct price band and has positive qty
inline bool isValid(Side side, int tick, long qty) {
    if (qty <= 0) return false;
    if (side == Side::Buy  && (tick < MIN_BID || tick > MAX_BID)) return false;
    if (side == Side::Sell && (tick < MIN_ASK || tick > MAX_ASK)) return false;
    return true;
}
