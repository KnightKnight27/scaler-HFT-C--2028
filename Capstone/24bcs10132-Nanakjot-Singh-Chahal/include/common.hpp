// Shared types for all three order books.
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

// Prices are kept as integer ticks internally (1 tick = 0.01).
// The public API still takes/returns double so it matches the spec.
namespace ticks {
constexpr int kMinBid = 5000;   // 50.00
constexpr int kMaxBid = 9999;   // 99.99
constexpr int kMinAsk = 10001;  // 100.01
constexpr int kMaxAsk = 15000;  // 150.00
constexpr int kMin = kMinBid;
constexpr int kMax = kMaxAsk;
constexpr int kCount = kMax - kMin + 1;  // 10001 slots (100.00 is never used)

inline int from_price(double price) { return static_cast<int>(std::llround(price * 100.0)); }
inline double to_price(int tick) { return tick / 100.0; }

// True when price is on the 0.01 grid and inside the band for that side.
inline bool valid(Side side, double price, int& tick_out) {
    const int t = from_price(price);
    if (std::fabs(t / 100.0 - price) > 1e-9) return false;
    if (side == Side::Buy) {
        if (t < kMinBid || t > kMaxBid) return false;
    } else {
        if (t < kMinAsk || t > kMaxAsk) return false;
    }
    tick_out = t;
    return true;
}
}  // namespace ticks

// Per-order record. Every book needs side + price + qty to undo a cancel.
struct OrderInfo {
    int tick;
    Side side;
    long qty;
};
