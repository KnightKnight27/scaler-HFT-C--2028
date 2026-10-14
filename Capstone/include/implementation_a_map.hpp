#pragma once

#include "order_book_types.hpp"

#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

class MapOrderBook {
public:
    void add_order(std::uint64_t id, Side side, double price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct Order {
        Side side;
        int price;
        long qty;
    };

    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<std::uint64_t, Order> orders_;
};
