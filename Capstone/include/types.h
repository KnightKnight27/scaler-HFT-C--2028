// types.h — public API types (as given in the assignment) plus tick helpers.
//
// The public API keeps `double` prices exactly as specified. Internally every
// implementation works in integer ticks (1 tick = 0.01), converting once at the
// API boundary. Floating-point keys are a correctness hazard: 100.01 * 100 can be
// 10000.999..., so conversion always rounds (llround), never truncates.
#pragma once

#include <cmath>
#include <cstdint>

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

// Price bands from the assignment. Tick 10000 (100.00) is the mid and never valid.
constexpr Tick kMinBidTick = 5000;   // 50.00
constexpr Tick kMaxBidTick = 9999;   // 99.99
constexpr Tick kMidTick = 10000;     // 100.00
constexpr Tick kMinAskTick = 10001;  // 100.01
constexpr Tick kMaxAskTick = 15000;  // 150.00

// Full tick span including the unused mid slot: 5000..15000 -> 10,001 slots.
constexpr Tick kMinTick = kMinBidTick;
constexpr Tick kMaxTick = kMaxAskTick;
constexpr int kNumTicks = kMaxTick - kMinTick + 1;

inline Tick price_to_tick(double price) {
    return static_cast<Tick>(std::llround(price * 100.0));
}

// Every implementation (and the oracle) converts back through this one function,
// so equal ticks always produce bit-identical doubles and results compare exactly.
inline double tick_to_price(Tick tick) {
    return static_cast<double>(tick) / 100.0;
}

inline bool is_valid_tick(Side side, Tick tick) {
    return side == Side::Buy ? (tick >= kMinBidTick && tick <= kMaxBidTick)
                             : (tick >= kMinAskTick && tick <= kMaxAskTick);
}

// What every implementation remembers about a live order. No per-order queue at a
// level is needed: there is no matching, so cancel only has to subtract `qty` from
// the order's level.
struct OrderInfo {
    Side side;
    Tick tick;
    long qty;
};
