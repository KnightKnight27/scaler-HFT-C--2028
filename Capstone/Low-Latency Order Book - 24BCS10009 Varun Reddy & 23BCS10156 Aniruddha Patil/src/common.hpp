// Shared types and price helpers used by all three order-book implementations.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

enum class Side : uint8_t { Buy, Sell };

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

// ---------------------------------------------------------------------------
// Prices are kept as integer ticks (1 tick = 0.01) inside every book.
// The public API still takes and returns doubles; conversion happens only at
// the boundary, so no book ever compares or sums floating-point prices.
// ---------------------------------------------------------------------------
namespace px {

constexpr int32_t kBidMinTick = 5000;   //  50.00
constexpr int32_t kBidMaxTick = 9999;   //  99.99
constexpr int32_t kAskMinTick = 10001;  // 100.01
constexpr int32_t kAskMaxTick = 15000;  // 150.00

// Tick index space used by array-based books: index = tick - kMinTick.
// Index 5000 (price 100.00, the mid) is never occupied.
constexpr int32_t kMinTick = kBidMinTick;
constexpr int32_t kNumTicks = kAskMaxTick - kMinTick + 1;  // 10001 slots, 10000 usable

// Largest single-order quantity accepted (lets compact books store qty in 32 bits).
constexpr long kMaxOrderQty = 2147483647L;

inline double to_price(int32_t tick) { return tick / 100.0; }

// Converts a double price to ticks. Fails if the price is not a multiple of 0.01.
inline bool to_tick(double price, int32_t& tick) {
    if (!(price > 0.0 && price < 1e7)) return false;  // also rejects NaN
    const double scaled = price * 100.0;
    const double rounded = std::nearbyint(scaled);
    if (std::fabs(scaled - rounded) > 1e-6) return false;
    tick = static_cast<int32_t>(rounded);
    return true;
}

inline bool in_band(Side side, int32_t tick) {
    return side == Side::Buy ? (tick >= kBidMinTick && tick <= kBidMaxTick)
                             : (tick >= kAskMinTick && tick <= kAskMaxTick);
}

// Common validation shared by every implementation so they all accept and reject
// exactly the same orders. On success, `tick` holds the price in ticks.
inline bool validate_order(Side side, double price, long qty, int32_t& tick) {
    return qty > 0 && qty <= kMaxOrderQty && to_tick(price, tick) && in_band(side, tick);
}

}  // namespace px
