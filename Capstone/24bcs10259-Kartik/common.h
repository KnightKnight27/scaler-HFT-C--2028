#pragma once
#include <cstdint>
#include <cmath>
#include <vector>

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

// prices stored as ticks internally (100.01 -> 10001)
const int MIN_TICK = 5000;
const int MAX_TICK = 15000;
const int MAX_BID_TICK = 9999;   // 99.99
const int MIN_ASK_TICK = 10001;  // 100.01

inline int price_to_tick(double price) {
    return (int)std::llround(price * 100.0);
}

inline double tick_to_price(int tick) {
    return tick / 100.0;
}

inline bool valid_tick(Side side, int tick) {
    if (side == Side::Buy)
        return tick >= MIN_TICK && tick <= MAX_BID_TICK;
    return tick >= MIN_ASK_TICK && tick <= MAX_TICK;
}
