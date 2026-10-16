#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>
#include <algorithm>

#include "order_book.hpp"

class OrderBookA {
private:
    struct OrderDetails {
        Side side;
        int price_tick;
        long qty;
    };

    std::unordered_map<uint64_t, OrderDetails> orders_;

    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long, std::less<int>> asks_;

    static double tick_to_price(int tick) {
        return static_cast<double>(tick) / 100.0;
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};