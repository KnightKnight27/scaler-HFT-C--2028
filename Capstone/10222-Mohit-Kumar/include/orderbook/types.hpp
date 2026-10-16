#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ob {
using Tick = int;
enum class Side { Buy, Sell };
struct Level { Tick price; long qty; };
struct BBO {
    bool has_bid = false;
    Tick bid_price = 0;
    long bid_qty = 0;
    bool has_ask = false;
    Tick ask_price = 0;
    long ask_qty = 0;
};
struct Order { Side side; Tick price; long qty; };
constexpr int levels_per_side = 5000;
inline int base_tick(Side side) { return side == Side::Buy ? 5000 : 10001; }
inline void validate(Side side, Tick price, long qty) {
    if (side != Side::Buy && side != Side::Sell)
        throw std::invalid_argument("invalid side");
    if (price < base_tick(side) || price >= base_tick(side) + levels_per_side)
        throw std::invalid_argument("price outside side's tick band");
    if (qty <= 0) throw std::invalid_argument("quantity must be positive");
}
inline void check_sum(long current, long added) {
    if (added > std::numeric_limits<long>::max() - current)
        throw std::overflow_error("level quantity overflow");
}
inline bool operator==(const Level& a, const Level& b) {
    return a.price == b.price && a.qty == b.qty;
}
inline bool operator==(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price &&
           a.bid_qty == b.bid_qty && a.has_ask == b.has_ask &&
           a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}
} // namespace ob
