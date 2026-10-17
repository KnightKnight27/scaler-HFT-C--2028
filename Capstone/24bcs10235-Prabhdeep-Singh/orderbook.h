#pragma once
#include <cassert>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long   qty;
};

struct BBO {
    bool   has_bid   = false;
    double bid_price = 0.0;
    long   bid_qty   = 0;
    bool   has_ask   = false;
    double ask_price = 0.0;
    long   ask_qty   = 0;
};

// Tick size is 0.01, so price 99.99 is tick 9999. Tick 10000 (the 100.00 mid) is never used.
constexpr int MIN_BID_TICK = 5000;    // 50.00
constexpr int MAX_BID_TICK = 9999;    // 99.99
constexpr int MIN_ASK_TICK = 10001;   // 100.01
constexpr int MAX_ASK_TICK = 15000;   // 150.00

inline int    price_to_tick(double p) { return static_cast<int>(p * 100.0 + 0.5); }
inline double tick_to_price(int t)    { return t * 0.01; }

// add_order() requires an in-band price for its side; every implementation asserts this.
inline bool in_band(Side side, int tick) {
    return side == Side::Buy ? (tick >= MIN_BID_TICK && tick <= MAX_BID_TICK)
                             : (tick >= MIN_ASK_TICK && tick <= MAX_ASK_TICK);
}
