
#pragma once

#include "order_book.hpp"

#include <array>
#include <cstddef>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace orderbook {

class ArrayOrderBook final : public OrderBook {
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
private:
    static constexpr Price MIN_BID = 5000;
    static constexpr Price MAX_BID = 9999;
    static constexpr Price MIN_ASK = 10001;
    static constexpr Price MAX_ASK = 15000;
    static constexpr std::size_t LEVEL_COUNT = 5000;
    struct PriceLevel {
        Quantity total_qty = 0;
        std::list<OrderId> fifo;
        bool active = false;
    };

    struct ActiveOrder {
        Side side;
        Price price;
        Quantity qty;
        std::size_t index;
        std::list<OrderId>::iterator fifo_it;
    };

    std::array<PriceLevel, LEVEL_COUNT> bids_{};
    std::array<PriceLevel, LEVEL_COUNT> asks_{};
    std::unordered_map<OrderId, ActiveOrder> active_orders_;
    std::unordered_set<OrderId> used_ids_;
    int best_bid_index_ = -1;
    int best_ask_index_ = static_cast<int>(LEVEL_COUNT);
    static bool valid_price(Side side, Price price);
    void update_best_bid();
    void update_best_ask();
};

}
