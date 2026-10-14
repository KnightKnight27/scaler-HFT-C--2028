#pragma once

#include "order_book_types.hpp"

#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

class IndexedOrderBook {
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

    std::unordered_map<int, long> bid_qty_;
    std::unordered_map<int, long> ask_qty_;
    std::set<int, std::greater<int>> bid_prices_;
    std::set<int> ask_prices_;
    std::map<std::uint64_t, Order> orders_;
};
