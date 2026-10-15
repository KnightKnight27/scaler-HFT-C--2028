// Types and price helpers shared by all three order book implementations.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}

struct BBO {
    bool has_bid = false;
    double bid_price = 0.0;
    long bid_qty = 0;
    bool has_ask = false;
    double ask_price = 0.0;
    long ask_qty = 0;
};

inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}

namespace book {

// Prices are stored internally as integer ticks (1 tick = 0.01).
// 100.01 -> 10001, 99.99 -> 9999.
using Tick = int32_t;

constexpr Tick kMinBidTick = 5000;   // 50.00
constexpr Tick kMaxBidTick = 9999;   // 99.99
constexpr Tick kMinAskTick = 10001;  // 100.01
constexpr Tick kMaxAskTick = 15000;  // 150.00
constexpr Tick kMinTick = kMinBidTick;
constexpr Tick kMaxTick = kMaxAskTick;

inline double to_price(Tick t) { return t / 100.0; }

// Validates an incoming order and converts its price to ticks.
// Rejects: qty <= 0, non-finite price, price not on the 0.01 grid,
// price outside the band for its side.
inline bool to_valid_tick(Side side, double price, long qty, Tick& out) {
    if (qty <= 0 || !std::isfinite(price) || price < 0.0 || price > 1000.0) return false;
    const double scaled = price * 100.0;
    const double rounded = std::round(scaled);
    if (std::fabs(scaled - rounded) > 1e-6) return false;
    const Tick t = static_cast<Tick>(rounded);
    if (side == Side::Buy ? (t < kMinBidTick || t > kMaxBidTick)
                          : (t < kMinAskTick || t > kMaxAskTick))
        return false;
    out = t;
    return true;
}

}  // namespace book
