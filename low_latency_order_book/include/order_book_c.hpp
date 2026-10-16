#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

#include "order_book.hpp"

class OrderBookC {
private:
    struct OrderInfo {
        Side side;
        int price_tick;
        long qty;
    };

    std::unordered_map<uint64_t, OrderInfo> orders_;

    std::vector<long> price_quantities_;

    int best_bid_tick_;
    int best_ask_tick_;

    static double tick_to_price(int tick) {
        return static_cast<double>(tick) / 100.0;
    }

public:
    OrderBookC();

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};