// Implementation A: balanced trees for price levels + chained hash map for orders.
//
//   price levels : std::map<tick, total_qty>, one per side (red-black tree)
//                  bids ordered high->low, asks low->high, so begin() is the best price
//   orders       : std::unordered_map<id, {side, tick, qty}>
//   best price   : bids_.begin() / asks_.begin()  (std::map caches its leftmost node)
//   cancel       : hash lookup by id, then tree lookup of the order's price level
#pragma once

#include <functional>
#include <map>
#include <unordered_map>

#include "common.hpp"

class OrderBookMap {
public:
    static constexpr const char* kName = "A: std::map levels + std::unordered_map orders";

    bool add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
    size_t order_count() const { return orders_.size(); }

private:
    struct Order {
        int32_t tick;
        Side side;
        long qty;
    };

    std::map<int32_t, long, std::greater<int32_t>> bids_;
    std::map<int32_t, long, std::less<int32_t>> asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
