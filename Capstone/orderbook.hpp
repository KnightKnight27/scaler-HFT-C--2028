// Rohan Ranjan-10428-Capstone project
//
// things shared by all three order books: the API types, tick helpers
// and the rule for which orders are valid

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long   qty;
};

struct BBO {
    bool   has_bid   = false;
    double bid_price = 0;
    long   bid_qty   = 0;

    bool   has_ask   = false;
    double ask_price = 0;
    long   ask_qty   = 0;
};

// every book stores prices as integer ticks (1 tick = 0.01)
// so 99.99 -> 9999 and 100.01 -> 10001, no float compares anywhere inside
constexpr int kMinBidTick = 5000;   //  50.00
constexpr int kMaxBidTick = 9999;   //  99.99
constexpr int kMinAskTick = 10001;  // 100.01
constexpr int kMaxAskTick = 15000;  // 150.00
constexpr int kNumTicks   = kMaxAskTick - kMinBidTick + 1; // 10001 slots, 100.00 is never used

inline double to_price(int tick) { return tick / 100.0; }

// turns a double price into ticks and checks the order is allowed
// same rule in all three books, so they all ignore the same bad orders:
//   qty must be > 0, price must be a multiple of 0.01,
//   bids in [50.00, 99.99], asks in [100.01, 150.00]
inline bool parse_order(Side side, double price, long qty, int& tick) {
    if (qty <= 0 || !std::isfinite(price)) return false;
    double scaled = price * 100.0;
    if (scaled < kMinBidTick - 1 || scaled > kMaxAskTick + 1) return false;
    tick = static_cast<int>(std::llround(scaled));
    if (std::fabs(scaled - tick) > 1e-6) return false; // e.g. 99.995
    if (side == Side::Buy) return tick >= kMinBidTick && tick <= kMaxBidTick;
    return tick >= kMinAskTick && tick <= kMaxAskTick;
}
