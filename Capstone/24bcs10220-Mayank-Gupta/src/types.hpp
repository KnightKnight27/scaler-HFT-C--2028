#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ob {

using OrderId = std::uint64_t;
using Quantity = std::int64_t;
using Price = int; // Integer ticks: 9950 means 99.50.

constexpr Price bid_min = 5000;
constexpr Price bid_max = 9999;
constexpr Price ask_min = 10001;
constexpr Price ask_max = 15000;
constexpr int levels_per_side = 5000;

enum class Side { Buy, Sell };

struct Order {
    Side side;
    Price price;
    Quantity qty;
};

struct Level {
    Price price;
    Quantity qty;
};

struct BBO {
    bool has_bid = false;
    Price bid_price = 0;
    Quantity bid_qty = 0;
    bool has_ask = false;
    Price ask_price = 0;
    Quantity ask_qty = 0;
};

inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}

inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}

inline void validate_order(Side side, Price price, Quantity qty) {
    const bool valid_price =
        (side == Side::Buy && price >= bid_min && price <= bid_max) ||
        (side == Side::Sell && price >= ask_min && price <= ask_max);
    if (!valid_price || qty <= 0) {
        throw std::invalid_argument("order must have a valid side, price and positive quantity");
    }
}

inline void check_quantity_sum(Quantity current, Quantity added) {
    if (added > std::numeric_limits<Quantity>::max() - current) {
        throw std::overflow_error("price-level quantity overflow");
    }
}

} // namespace ob

