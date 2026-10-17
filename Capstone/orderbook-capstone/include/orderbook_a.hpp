#pragma once

#include "types.hpp"
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

class OrderBookA {
public:
    OrderBookA();
    ~OrderBookA() = default;

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct OrderInfo {
        Side side;
        int32_t tick;
        long qty;
    };

    // Bids sorted descending (highest tick first); Asks sorted ascending (lowest tick first)
    std::map<int32_t, long, std::greater<int32_t>> bids_;
    std::map<int32_t, long, std::less<int32_t>> asks_;
    std::unordered_map<uint64_t, OrderInfo> orders_;
};