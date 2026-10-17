#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace orderbook {

using Price = std::int32_t;
using Quantity = std::int64_t;
using OrderId = std::uint64_t;

enum class Side {
    Buy,
    Sell
};

struct Level {
    Price price{};
    Quantity qty{};
};

struct BBO {
    bool has_bid = false;
    Price bid_price = 0;
    Quantity bid_qty = 0;

    bool has_ask = false;
    Price ask_price = 0;
    Quantity ask_qty = 0;
};

class OrderBook {
public:
    virtual ~OrderBook() = default;

    virtual bool add_order(
        OrderId id, Side side, Price price, Quantity qty
    ) = 0;

    virtual bool cancel_order(OrderId id) = 0;

    virtual BBO get_bbo() const = 0;

    virtual std::vector<Level> get_bids(
        std::size_t depth = 10
    ) const = 0;

    virtual std::vector<Level> get_asks(
        std::size_t depth = 10
    ) const = 0;
};

}