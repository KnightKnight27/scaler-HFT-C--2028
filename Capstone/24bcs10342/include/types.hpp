// Shared types and constants for all three order book implementations.
//
// INTERFACE CHANGE (documented in README): prices are integer ticks
// (1 tick = 0.01), not doubles.  100.01 -> 10001.  add_order returns bool
// (false = rejected: bad price/qty or duplicate id).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

enum class Side : uint8_t { Buy, Sell };

constexpr int kMinBidTick = 5000;   // 50.00
constexpr int kMaxBidTick = 9999;   // 99.99
constexpr int kMinAskTick = 10001;  // 100.01
constexpr int kMaxAskTick = 15000;  // 150.00

inline int to_ticks(double price) { return static_cast<int>(std::llround(price * 100.0)); }
inline double to_price(int ticks) { return ticks / 100.0; }
inline std::string fmt_price(int ticks) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d.%02d", ticks / 100, ticks % 100);
    return buf;
}

inline bool valid_tick(Side s, int t) {
    return s == Side::Buy ? (t >= kMinBidTick && t <= kMaxBidTick)
                          : (t >= kMinAskTick && t <= kMaxAskTick);
}

struct Level {
    int price;  // ticks
    long qty;
};
inline bool operator==(const Level& a, const Level& b) { return a.price == b.price && a.qty == b.qty; }
inline bool operator!=(const Level& a, const Level& b) { return !(a == b); }

struct BBO {
    bool has_bid;
    int bid_price;  // ticks
    long bid_qty;
    bool has_ask;
    int ask_price;  // ticks
    long ask_qty;
};
inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}
inline bool operator!=(const BBO& a, const BBO& b) { return !(a == b); }
