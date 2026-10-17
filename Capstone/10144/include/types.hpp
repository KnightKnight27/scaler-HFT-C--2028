#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace ob {
enum class Side { Buy, Sell };
using Qty = std::int64_t;
struct Order { std::uint64_t id; Side side; int price; Qty qty; };
struct Level {
    int price = 0; // integer cents, e.g. 10001 = 100.01
    Qty qty = 0;
    bool operator==(const Level& r) const { return price == r.price && qty == r.qty; }
};
struct BBO {
    bool has_bid = false;
    int bid_price = 0;
    Qty bid_qty = 0;
    bool has_ask = false;
    int ask_price = 0;
    Qty ask_qty = 0;
    bool operator==(const BBO& r) const {
        return has_bid == r.has_bid && bid_price == r.bid_price && bid_qty == r.bid_qty &&
               has_ask == r.has_ask && ask_price == r.ask_price && ask_qty == r.ask_qty;
    }
};
inline void validate(Side side, int price, Qty qty) {
    if (side != Side::Buy && side != Side::Sell) throw std::invalid_argument("invalid side");
    const int low = side == Side::Buy ? 5000 : 10001;
    if (price < low || price > low + 4999) throw std::invalid_argument("price outside side band");
    if (qty <= 0) throw std::invalid_argument("quantity must be positive");
}
inline void check_sum(Qty current, Qty extra) {
    if (extra > std::numeric_limits<Qty>::max() - current)
        throw std::overflow_error("level quantity exceeds int64_t");
}
inline BBO make_bbo(Level bid, Level ask) {
    return {bid.qty != 0, bid.price, bid.qty, ask.qty != 0, ask.price, ask.qty};
}
} // namespace ob
