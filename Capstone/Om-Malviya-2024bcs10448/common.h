// common.h — shared types and price<->tick helpers for all three order books.
//
// The assignment's required public API uses `double price`. Internally every
// implementation works in INTEGER TICKS (Hint 3) because the tick size is fixed
// at 0.01, so prices are exact integers of ticks. We convert at the API boundary
// only. This is the single interface note documented in the README.
//
//   price 100.01  ->  tick 10001
//   price  99.99  ->  tick  9999
//
// Price bands (fixed by the spec):
//   Bid band: [50.00, 99.99]   -> ticks [5000, 9999]   (5000 levels)
//   Ask band: [100.01, 150.00] -> ticks [10001, 15000] (5000 levels)
//   Mid price 100.00 (tick 10000) is never a resting level.
//   => 10,000 possible price levels in total.

#ifndef CAPSTONE_COMMON_H
#define CAPSTONE_COMMON_H

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

// ---- tick constants -------------------------------------------------------
namespace ticks {
constexpr int kBidMin = 5000;    // 50.00
constexpr int kBidMax = 9999;    // 99.99
constexpr int kAskMin = 10001;   // 100.01
constexpr int kAskMax = 15000;   // 150.00
constexpr int kLevelsPerSide = 5000;

inline int to_tick(double price) {
    // Round to nearest tick; exact for all valid inputs in the bands.
    return static_cast<int>(std::llround(price * 100.0));
}

inline double to_price(int tick) {
    return static_cast<double>(tick) / 100.0;
}
}  // namespace ticks

#endif  // CAPSTONE_COMMON_H
