// common.h
// Shared types and price helpers used by all three order books.
#pragma once

#include <cmath>
#include <cstdint>

enum class Side { Buy, Sell };

// Price range, stored as integer ticks (1 tick = 0.01)
constexpr int BID_MIN_TICK = 5000;    // 50.00
constexpr int BID_MAX_TICK = 9999;    // 99.99
constexpr int ASK_MIN_TICK = 10001;   // 100.01
constexpr int ASK_MAX_TICK = 15000;   // 150.00
constexpr int LEVELS_PER_SIDE = 5000; // 5000 bid levels + 5000 ask levels = 10,000

// One price level: a price (in ticks) and the total quantity at it
struct Level {
    int  tick;
    long qty;
};

// Best bid and best ask together
struct BBO {
    bool has_bid;
    int  bid_tick;
    long bid_qty;
    bool has_ask;
    int  ask_tick;
    long ask_qty;
};

// Needed so the correctness test can compare results from different books
inline bool operator==(const Level& a, const Level& b) {
    return a.tick == b.tick && a.qty == b.qty;
}

inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_tick == b.bid_tick &&
           a.bid_qty == b.bid_qty && a.has_ask == b.has_ask &&
           a.ask_tick == b.ask_tick && a.ask_qty == b.ask_qty;
}

// ---- Price conversion ----

// 99.98 -> 9998. I use round() because 99.98 * 100 can come out as 9997.999...
inline int price_to_tick(double price) {
    return static_cast<int>(std::llround(price * 100.0));
}

// 9998 -> 99.98 (only used when printing)
inline double tick_to_price(int tick) {
    return tick / 100.0;
}

// ---- Array index helpers (used by Implementation C) ----

inline int bid_index(int tick) { return tick - BID_MIN_TICK; }  // 0..4999
inline int ask_index(int tick) { return tick - ASK_MIN_TICK; }  // 0..4999

inline int bid_tick_from_index(int idx) { return idx + BID_MIN_TICK; }
inline int ask_tick_from_index(int idx) { return idx + ASK_MIN_TICK; }

// Check that a tick is allowed for its side
inline bool is_valid_tick(Side side, int tick) {
    if (side == Side::Buy)
        return tick >= BID_MIN_TICK && tick <= BID_MAX_TICK;
    return tick >= ASK_MIN_TICK && tick <= ASK_MAX_TICK;
}
