#pragma once
// Types shared by all three order books.
// The API takes double prices, but inside we work with integer ticks
// (price * 100) because the tick size is fixed at 0.01.

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

using Tick = int32_t;

// Bids are 50.00 .. 99.99 and asks are 100.01 .. 150.00, 5000 levels each.
constexpr Tick kMinBidTick = 5000;
constexpr Tick kMaxBidTick = 9999;
constexpr Tick kMinAskTick = 10001;
constexpr Tick kMaxAskTick = 15000;
constexpr int kLevelsPerSide = 5000;

inline Tick to_tick(double price) {
    return (Tick)std::lround(price * 100.0);
}

inline double to_price(Tick tick) {
    return tick / 100.0;
}

// Anything below 100.00 is a bid, so the tick alone tells the side.
inline bool is_bid_tick(Tick tick) {
    return tick <= kMaxBidTick;
}

inline bool tick_in_band(Side side, Tick tick) {
    if (side == Side::Buy) return tick >= kMinBidTick && tick <= kMaxBidTick;
    return tick >= kMinAskTick && tick <= kMaxAskTick;
}
