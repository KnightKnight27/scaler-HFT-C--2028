#pragma once

#include <cstdint>
#include <string>
#include <vector>

// price is kept in ticks of 0.01, so 100.01 is stored as 10001
using Price = uint64_t;

enum class Side { Buy, Sell };

struct Level {
    Price price;
    long qty;
};

struct BBO {
    bool has_bid = false;
    Price bid_price = 0;
    long bid_qty = 0;

    bool has_ask = false;
    Price ask_price = 0;
    long ask_qty = 0;
};

struct Order {
    Side side;
    Price price;
    long qty;
};

const Price BID_LOW = 5000;    // 50.00
const Price BID_HIGH = 9999;   // 99.99
const Price ASK_LOW = 10001;   // 100.01
const Price ASK_HIGH = 15000;  // 150.00
const int LEVELS_PER_SIDE = 5000;

inline bool valid_order(Side side, Price price, long qty) {
    if (qty <= 0) return false;
    if (side == Side::Buy) return price >= BID_LOW && price <= BID_HIGH;
    return price >= ASK_LOW && price <= ASK_HIGH;
}

inline std::string price_str(Price p) {
    std::string cents = std::to_string(p % 100);
    if (cents.size() == 1) cents = "0" + cents;
    return std::to_string(p / 100) + "." + cents;
}

inline bool same_bbo(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask &&
           a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}

inline bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    }
    return true;
}
