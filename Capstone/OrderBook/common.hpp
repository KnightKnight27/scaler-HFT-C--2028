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

struct Order {
    int tick;
    long qty;
    Side side;
};

// 1 tick = 0.01, so 50.00 -> 5000 and 150.00 -> 15000
constexpr int MIN_TICK = 5000;
constexpr int MAX_TICK = 15000;
constexpr int BID_HI = 9999;
constexpr int ASK_LO = 10001;

inline int to_tick(double price) { return (int)std::lround(price * 100.0); }
inline double to_price(int tick) { return tick / 100.0; }

inline bool valid(Side side, int tick) {
    if (side == Side::Buy) return tick >= MIN_TICK && tick <= BID_HI;
    return tick >= ASK_LO && tick <= MAX_TICK;
}
