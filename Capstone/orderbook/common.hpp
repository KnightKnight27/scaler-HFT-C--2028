// Shared types for all three order book implementations.
//
// One small change from the spec: prices are stored internally as integer
// ticks (cents) instead of double. Tick size is 0.01, so price * 100 is
// always a whole number. This avoids floating point rounding issues and is
// faster. The public API still takes/returns double, so nothing changes
// for the caller.

#pragma once

#include <cstdint>
#include <vector>
#include <cmath>

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

// price 99.99 -> tick 9999. Adding 0.5 before truncating just rounds
// to the nearest int instead of always rounding down.
inline int price_to_tick(double price) {
    return (int)std::llround(price * 100.0);
}

inline double tick_to_price(int tick) {
    return tick / 100.0;
}
