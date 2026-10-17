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

// prices are stored inside the books as integer ticks (1 tick = 0.01)
// 50.00 -> 5000, 100.01 -> 10001, 150.00 -> 15000
const int MIN_BID_TICK = 5000;
const int MAX_BID_TICK = 9999;
const int MIN_ASK_TICK = 10001;
const int MAX_ASK_TICK = 15000;

inline int to_tick(double price) {
    return (int)std::llround(price * 100.0);
}

inline double to_price(int tick) {
    return tick / 100.0;
}

inline bool valid_price(Side side, int tick) {
    if (side == Side::Buy)
        return tick >= MIN_BID_TICK && tick <= MAX_BID_TICK;
    return tick >= MIN_ASK_TICK && tick <= MAX_ASK_TICK;
}
