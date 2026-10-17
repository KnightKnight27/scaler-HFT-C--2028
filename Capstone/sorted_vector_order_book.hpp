// Data structure: sorted std::vector price levels plus std::unordered_map order-ID index.
#pragma once

#include "order_book.hpp"

#include <unordered_map>
#include <vector>

namespace orderbook {

class SortedVectorOrderBook {
public:
    void add_order(std::uint64_t id, Side side, PriceTicks price_ticks, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct Order {
        Side side;
        PriceTicks price_ticks;
        long qty;
        bool live = true;
    };

    std::vector<Level> bids_;
    std::vector<Level> asks_;
    std::unordered_map<std::uint64_t, Order> orders_;
};

}  // namespace orderbook
