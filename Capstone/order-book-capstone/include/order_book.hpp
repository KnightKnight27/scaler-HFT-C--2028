#pragma once
// =============================================================================
// order_book.hpp  –  Shared types and abstract interface
// =============================================================================
// Price rules
// -----------
//   Valid bids  : 50.00 .. 99.99  (ticks  5000 ..  9999)
//   Valid asks  : 100.01 .. 150.00 (ticks 10001 .. 15000)
//   Midpoint    : 100.00           (tick  10000, never valid)
//   Tick size   : 0.01
//
// Internally every price is stored as an integer tick = round(price * 100).
// Conversion to double happens only at the API boundary.
//
// Validation policy
// -----------------
//   add_order   – throws std::invalid_argument on invalid price, invalid
//                 quantity (<= 0), or duplicate order ID.
//   cancel_order – returns false for unknown or already-cancelled IDs.
//   get_bbo     – returns has_bid==false / has_ask==false for an empty side.
//   get_bids / get_asks – return only the levels that exist; depth <= 0 → {}.
// =============================================================================

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>
#include <cmath>
#include <limits>

namespace hft {

// ---------------------------------------------------------------------------
// Price-domain constants (all in integer ticks, 1 tick = 0.01)
// ---------------------------------------------------------------------------
constexpr int TICK_BID_MIN  =  5000;   //  50.00
constexpr int TICK_BID_MAX  =  9999;   //  99.99
constexpr int TICK_MID      = 10000;   // 100.00  (invalid)
constexpr int TICK_ASK_MIN  = 10001;   // 100.01
constexpr int TICK_ASK_MAX  = 15000;   // 150.00
constexpr int TOTAL_TICKS   = 10000;   // valid price levels (5000 bid + 5000 ask)

// ---------------------------------------------------------------------------
// Public-facing value types
// ---------------------------------------------------------------------------
enum class Side { Buy, Sell };

struct Level {
    double price;
    long   qty;
};

struct BBO {
    bool   has_bid;
    double bid_price;
    long   bid_qty;
    bool   has_ask;
    double ask_price;
    long   ask_qty;
};

// ---------------------------------------------------------------------------
// Tick <-> double conversion
// ---------------------------------------------------------------------------
inline double tick_to_price(int tick) noexcept { return tick / 100.0; }

// ---------------------------------------------------------------------------
// Robust price-to-tick conversion with strict validation.
//
// Returns the integer tick for a price, or throws std::invalid_argument if:
//   - price is NaN or infinite
//   - price is outside the representable range [50.00, 150.00]
//   - price is not a multiple of 0.01 within a tolerance of 1e-7
//     (this tolerance handles IEEE-754 representation errors for exact tick
//      values but is far too small to accept genuinely off-tick prices such as
//      99.991, which differs from the nearest tick by 0.001 >> 1e-9)
//
// The function multiplies by 100, checks that the result rounds to an integer
// within 1e-7 of the computed value, and converts to int only after all
// checks pass.  No implicit rounding of off-tick prices is performed.
// ---------------------------------------------------------------------------
inline int price_to_tick(double price)
{
    if (!std::isfinite(price)) {
        throw std::invalid_argument(
            "add_order: price is not finite (NaN or infinity)");
    }
    // Quick range guard before the multiplication to avoid overflow.
    // The valid range is [50.00, 150.00].
    if (price < 49.995 || price > 150.005) {
        throw std::invalid_argument(
            "add_order: price " + std::to_string(price) +
            " is outside the allowed range [50.00, 150.00]");
    }

    double scaled = price * 100.0;
    double rounded = std::round(scaled);

    // Tolerance of 1e-7 handles exact IEEE-754 values such as 99.99,
    // which may represent as 9998.999999... after *100.
    // It is far smaller than 0.001 (half a tick), so off-tick prices
    // like 99.991 (* 100 = 9999.1) are correctly rejected.
    if (std::fabs(scaled - rounded) > 1e-7) {
        throw std::invalid_argument(
            "add_order: price " + std::to_string(price) +
            " is not a valid 0.01 tick");
    }

    return static_cast<int>(rounded);
}

// ---------------------------------------------------------------------------
// Validation helpers (operate on already-converted ticks)
// ---------------------------------------------------------------------------
inline bool is_valid_bid_tick(int tick) noexcept {
    return tick >= TICK_BID_MIN && tick <= TICK_BID_MAX;
}
inline bool is_valid_ask_tick(int tick) noexcept {
    return tick >= TICK_ASK_MIN && tick <= TICK_ASK_MAX;
}
inline bool is_valid_tick_for_side(int tick, Side side) noexcept {
    return side == Side::Buy ? is_valid_bid_tick(tick) : is_valid_ask_tick(tick);
}

// ---------------------------------------------------------------------------
// Abstract base
// ---------------------------------------------------------------------------
class IOrderBook {
public:
    virtual ~IOrderBook() = default;

    // Throws std::invalid_argument on: non-finite price, off-tick price,
    // price outside valid range for side, qty <= 0, duplicate order ID.
    // Throws std::overflow_error if adding qty would overflow the level aggregate.
    virtual void add_order(std::uint64_t id, Side side, double price, long qty) = 0;

    // Returns true on success, false if id is unknown or already cancelled.
    virtual bool cancel_order(std::uint64_t id) = 0;

    // Returns has_bid/has_ask == false for an empty side.
    virtual BBO get_bbo() const = 0;

    // depth <= 0 returns {}. Returns all available levels if fewer than depth exist.
    // Bids: descending price. Asks: ascending price.
    virtual std::vector<Level> get_bids(int depth = 10) const = 0;
    virtual std::vector<Level> get_asks(int depth = 10) const = 0;
};

} // namespace hft
