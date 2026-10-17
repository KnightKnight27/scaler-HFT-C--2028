#pragma once
// Shared types for all three order books.
// The public API takes double prices (as in the brief); every book converts
// to integer ticks (price * 100) on entry and only stores ints internally.

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
    double bid_price = 0.0;
    long bid_qty = 0;

    bool has_ask = false;
    double ask_price = 0.0;
    long ask_qty = 0;
};

inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}

inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}

namespace ticks {
// 0.01 tick size: 50.00 -> 5000, 99.99 -> 9999, 100.01 -> 10001, 150.00 -> 15000
constexpr int kBidMin = 5000;
constexpr int kBidMax = 9999;
constexpr int kAskMin = 10001;
constexpr int kAskMax = 15000;
constexpr int kMaxTick = 15000;

// llround, not a cast: 100.01 * 100 is 10000.999999..., which a cast would truncate.
inline int from_price(double price) { return static_cast<int>(std::llround(price * 100.0)); }
inline double to_price(int tick) { return tick / 100.0; }

inline bool in_band(Side side, int tick) {
    return side == Side::Buy ? (tick >= kBidMin && tick <= kBidMax)
                             : (tick >= kAskMin && tick <= kAskMax);
}

// Rejects prices that are not an exact multiple of 0.01 (e.g. 100.005).
inline bool on_grid(double price, int tick) { return std::fabs(price * 100.0 - tick) < 1e-6; }
}  // namespace ticks
