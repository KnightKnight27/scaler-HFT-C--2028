// Common types and price helpers shared by the three order book builds.
//
// The assignment API talks in doubles, but the tick size is fixed at 0.01 so
// every legal price is really an integer number of ticks. I keep the public
// double signature and convert to an int tick on the way in. Doing the
// conversion once, in one place, means the three implementations only ever deal
// with ints and stay comparable.
#ifndef OB_COMMON_HPP
#define OB_COMMON_HPP

#include <cstdint>
#include <cmath>
#include <stdexcept>
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

// Tick ranges. Bids live in [50.00, 99.99] and asks in [100.01, 150.00], which
// is 5000 + 5000 = 10000 distinct price levels, mid at 100.00.
namespace ticks {
    constexpr int kBidMin = 5000;    // 50.00
    constexpr int kBidMax = 9999;    // 99.99
    constexpr int kAskMin = 10001;   // 100.01
    constexpr int kAskMax = 15000;   // 150.00
    constexpr int kBidCount = kBidMax - kBidMin + 1;
    constexpr int kAskCount = kAskMax - kAskMin + 1;
}

// A single resting order. All three books keep a record like this so cancel can
// undo exactly what add did.
struct OrderRec {
    Side side;
    int tick;
    long qty;
};

inline int price_to_tick(double price) {
    double scaled = price * 100.0;
    long rounded = std::lround(scaled);
    // Reject anything that is not a clean multiple of the tick size.
    if (std::fabs(scaled - static_cast<double>(rounded)) > 1e-6) {
        throw std::invalid_argument("price is not a multiple of the tick size");
    }
    return static_cast<int>(rounded);
}

inline double tick_to_price(int tick) {
    return static_cast<double>(tick) / 100.0;
}

// Shared input validation. Throws std::invalid_argument on anything the book is
// not allowed to hold. Kept tiny so it stays cheap in the add hot path.
inline void validate_new_order(Side side, int tick, long qty) {
    if (qty <= 0) {
        throw std::invalid_argument("quantity must be positive");
    }
    if (side == Side::Buy) {
        if (tick < ticks::kBidMin || tick > ticks::kBidMax) {
            throw std::invalid_argument("buy price outside the bid band");
        }
    } else {
        if (tick < ticks::kAskMin || tick > ticks::kAskMax) {
            throw std::invalid_argument("sell price outside the ask band");
        }
    }
}

#endif // OB_COMMON_HPP
