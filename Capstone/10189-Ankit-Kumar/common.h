// common.h - types and constants shared by all three order books.
//
// Interface change vs. the assignment: prices are integer ticks (1 tick = 0.01),
// not double. 99.98 is stored as 9998. Converting to/from double happens only at
// the edges (to_ticks / format_price), never inside the books.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using Price = int32_t;   // price in ticks
using Qty = int64_t;     // quantity (the assignment's `long`)
using OrderId = uint64_t;

enum class Side : uint8_t { Buy, Sell };

struct Level {
    Price price;
    Qty qty;

    bool operator==(const Level& o) const { return price == o.price && qty == o.qty; }
    bool operator!=(const Level& o) const { return !(*this == o); }
};

struct BBO {
    bool has_bid = false;
    Price bid_price = 0;
    Qty bid_qty = 0;

    bool has_ask = false;
    Price ask_price = 0;
    Qty ask_qty = 0;

    bool operator==(const BBO& o) const {
        return has_bid == o.has_bid && bid_price == o.bid_price && bid_qty == o.bid_qty &&
               has_ask == o.has_ask && ask_price == o.ask_price && ask_qty == o.ask_qty;
    }
    bool operator!=(const BBO& o) const { return !(*this == o); }
};

// ---- the rules of the game, in ticks ----
constexpr Price kBidMin = 5000;    //  50.00
constexpr Price kBidMax = 9999;    //  99.99
constexpr Price kAskMin = 10001;   // 100.01
constexpr Price kAskMax = 15000;   // 150.00
constexpr int kLevelsPerSide = kBidMax - kBidMin + 1;   // 5000, so 10,000 levels in total

// Largest quantity a single order may have. Every book rejects qty <= 0 or
// qty > kMaxOrderQty, so they all behave the same on bad input
// (book C stores an order's qty in 32 bits).
constexpr Qty kMaxOrderQty = 1'000'000'000;

inline bool valid_price(Side side, Price p) {
    return side == Side::Buy ? (p >= kBidMin && p <= kBidMax) : (p >= kAskMin && p <= kAskMax);
}

inline bool valid_qty(Qty q) { return q > 0 && q <= kMaxOrderQty; }

// 99.98 -> 9998. llround, because 99.98 * 100 is 9997.999999... in binary floating point.
inline Price to_ticks(double price) { return static_cast<Price>(std::llround(price * 100.0)); }

// 9998 -> "99.98"
inline std::string format_price(Price ticks) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d.%02d", ticks / 100, ticks % 100);
    return buf;
}
