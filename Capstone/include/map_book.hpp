#pragma once

#include "order_book.hpp"

#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace orderbook {

class MapOrderBook final : public OrderBook {
private:
    struct PriceLevel {
        Quantity total_qty = 0;
        std::list<OrderId> fifo;
    };

    struct ActiveOrder {
        Side side;
        Price price;
        Quantity qty;
        std::list<OrderId>::iterator fifo_it;
    };

    using BidLevels =
        std::map<Price, PriceLevel, std::greater<Price>>;

    using AskLevels =
        std::map<Price, PriceLevel>;

    BidLevels bids_;
    AskLevels asks_;

    std::unordered_map<OrderId, ActiveOrder> active_;
    std::unordered_set<OrderId> used_ids_;

    static bool valid_price(Side side, Price price);

public:
    bool add_order(
        OrderId id, Side side, Price price, Quantity qty
    ) override;

    bool cancel_order(OrderId id) override;

    BBO get_bbo() const override;

    std::vector<Level> get_bids(
        std::size_t depth = 10
    ) const override;

    std::vector<Level> get_asks(
        std::size_t depth = 10
    ) const override;
};

}