// common.hpp - types and constants shared by all three order books.
//
// INTERFACE CHANGE vs the PDF: prices are integer TICKS (1 tick = 0.01), not double.
//   100.01 -> 10001, 99.99 -> 9999.   to_ticks()/to_price() convert at the edges.
// add_order() returns bool (false = rejected) instead of void.
#pragma once
#include <cstdint>

using OrderId = std::uint64_t;
using Price   = std::int32_t;  // price in ticks

enum class Side : std::uint8_t { Buy, Sell };

// 5000 bid levels + 5000 ask levels = the 10,000 price levels from the spec.
constexpr Price kBidLoTick = 5000;    // 50.00
constexpr Price kBidHiTick = 9999;    // 99.99
constexpr Price kAskLoTick = 10001;   // 100.01
constexpr Price kAskHiTick = 15000;   // 150.00
constexpr int   kLevelsPerSide = 5000;

inline Price  to_ticks(double p) { return static_cast<Price>(p * 100.0 + 0.5); }
inline double to_price(Price t)  { return t / 100.0; }

inline bool valid_price(Side s, Price t) {
    return s == Side::Buy ? (t >= kBidLoTick && t <= kBidHiTick)
                          : (t >= kAskLoTick && t <= kAskHiTick);
}

struct Level {
    Price price;
    long  qty;
};

struct BBO {
    bool  has_bid;
    Price bid_price;
    long  bid_qty;
    bool  has_ask;
    Price ask_price;
    long  ask_qty;
};

inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}
inline bool operator!=(const Level& a, const Level& b) { return !(a == b); }

// Books always zero-initialise the unused side, so a plain field compare is enough.
inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}
inline bool operator!=(const BBO& a, const BBO& b) { return !(a == b); }
