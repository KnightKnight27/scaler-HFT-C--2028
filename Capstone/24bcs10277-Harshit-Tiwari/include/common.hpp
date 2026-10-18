#pragma once

#include <cmath>
#include <cstdint>
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

// Prices are integer ticks of 0.01, so 100.01 -> 10001.
constexpr int MIN_BID_TICK = 5000;
constexpr int MAX_BID_TICK = 9999;
constexpr int MIN_ASK_TICK = 10001;
constexpr int MAX_ASK_TICK = 15000;

inline int to_tick(double price) { return (int)std::llround(price * 100.0); }
inline double to_price(int tick) { return tick / 100.0; }

// Returns -1 if the price is not a valid tick for this side.
inline int checked_tick(Side side, double price) {
    int tick = to_tick(price);
    if (std::fabs(price * 100.0 - tick) > 1e-6) return -1;
    if (side == Side::Buy && (tick < MIN_BID_TICK || tick > MAX_BID_TICK)) return -1;
    if (side == Side::Sell && (tick < MIN_ASK_TICK || tick > MAX_ASK_TICK)) return -1;
    return tick;
}
