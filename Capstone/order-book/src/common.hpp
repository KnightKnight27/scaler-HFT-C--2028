#pragma once

// Types and price helpers shared by all three order books.

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side : std::uint8_t { Buy, Sell };

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

// Store prices as integer ticks internally: 99.50 -> 9950.
// Convert to/from double only at the public interface.
using Tick = std::int32_t;

constexpr Tick kMinBidTick = 5000;   // 50.00
constexpr Tick kMaxBidTick = 9999;   // 99.99
constexpr Tick kMinAskTick = 10001;  // 100.01
constexpr Tick kMaxAskTick = 15000;  // 150.00
constexpr int kLevelsPerSide = 5000;

// Largest quantity accepted for one order. Keeping it within 32 bits lets books
// B and C store an order in 16 bytes. Level totals are still kept as long.
constexpr long kMaxOrderQty = 2147483647;

inline double tick_to_price(Tick tick) { return tick / 100.0; }

inline bool is_bid_tick(Tick tick) { return tick <= kMaxBidTick; }

// Converts a price to ticks. Fails for NaN, infinity, prices outside
// [50.00, 150.00] and prices that are not a whole number of ticks.
inline bool price_to_tick(double price, Tick& tick) {
    const double scaled = price * 100.0;
    // Written so that NaN fails the check too.
    if (!(scaled > kMinBidTick - 0.5 && scaled < kMaxAskTick + 0.5)) return false;
    tick = static_cast<Tick>(scaled + 0.5);
    return std::fabs(scaled - tick) < 1e-6;
}

// Every book calls this first, so all of them accept and reject the same orders.
inline bool validate_order(Side side, double price, long qty, Tick& tick) {
    if (qty <= 0 || qty > kMaxOrderQty) return false;
    if (!price_to_tick(price, tick)) return false;
    if (side == Side::Buy) return tick >= kMinBidTick && tick <= kMaxBidTick;
    return tick >= kMinAskTick && tick <= kMaxAskTick;
}
