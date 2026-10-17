#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ob {
using OrderId = std::uint64_t;
using Price = std::int32_t;
using Quantity = std::int64_t;
constexpr int price_count = 5000;

enum class Side { Buy, Sell };

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

struct Order {
    Side side;
    Price price;
    Quantity qty;
};

inline void validate(Side side, Price price, Quantity qty) {
    const bool valid_bid = side == Side::Buy && price >= 5000 && price <= 9999;
    const bool valid_ask = side == Side::Sell && price >= 10001 && price <= 15000;
    if ((!valid_bid && !valid_ask) || qty <= 0)
        throw std::invalid_argument("invalid side, price or quantity");
}

inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}

inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) &&
           (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty));
}
}
