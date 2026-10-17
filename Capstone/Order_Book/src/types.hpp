// Common types shared by all three order book implementations.
//
// Prices are stored as integer ticks (1 tick = 0.01), so 100.01 is 10001.
// This is the one change we made to the interface given in the assignment.
#pragma once

#include <cstdint>
#include <vector>

enum class Side { Buy, Sell };

// price is in ticks
struct Level {
    int price;
    long qty;
};

struct BBO {
    bool has_bid = false;
    int bid_price = 0;
    long bid_qty = 0;

    bool has_ask = false;
    int ask_price = 0;
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

// Price bands from the assignment, in ticks.
constexpr int kMinBidTick = 5000;   // 50.00
constexpr int kMaxBidTick = 9999;   // 99.99
constexpr int kMinAskTick = 10001;  // 100.01
constexpr int kMaxAskTick = 15000;  // 150.00
constexpr int kNumTicks = kMaxAskTick + 1;  // array size for price indexed designs

inline double ticks_to_price(int ticks) { return ticks / 100.0; }

inline bool valid_tick(Side side, int tick) {
    if (side == Side::Buy) return tick >= kMinBidTick && tick <= kMaxBidTick;
    return tick >= kMinAskTick && tick <= kMaxAskTick;
}

// What we remember about a resting order so we can cancel it later.
struct Order {
    long qty;
    int tick;
    Side side;
};
