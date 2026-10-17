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
    bool has_bid = false;
    double bid_price = 0;
    long bid_qty = 0;

    bool has_ask = false;
    double ask_price = 0;
    long ask_qty = 0;
};

// Internally every book works in integer ticks (1 tick = 0.01).
// 50.00 -> 5000, 99.99 -> 9999, 100.01 -> 10001, 150.00 -> 15000
constexpr int MIN_BID_TICK = 5000;
constexpr int MAX_BID_TICK = 9999;
constexpr int MIN_ASK_TICK = 10001;
constexpr int MAX_ASK_TICK = 15000;

// qty is a long in the API but no sane order is bigger than this,
// and it lets book C keep an order in 16 bytes
constexpr long MAX_QTY = 2147483647L;

inline int to_tick(double price) { return static_cast<int>(std::llround(price * 100.0)); }
inline double to_price(int tick) { return tick / 100.0; }

// Converts a double price to ticks and checks it is on the 0.01 grid and
// inside the band for that side. All three books use this so they reject
// exactly the same orders.
inline bool price_to_tick(Side side, double price, int& tick) {
    if (!(price > 0)) return false;  // also catches NaN
    tick = to_tick(price);
    if (std::fabs(price * 100.0 - tick) > 1e-6) return false;  // not a multiple of 0.01
    if (side == Side::Buy) return tick >= MIN_BID_TICK && tick <= MAX_BID_TICK;
    return tick >= MIN_ASK_TICK && tick <= MAX_ASK_TICK;
}

inline bool valid_qty(long qty) { return qty > 0 && qty <= MAX_QTY; }
