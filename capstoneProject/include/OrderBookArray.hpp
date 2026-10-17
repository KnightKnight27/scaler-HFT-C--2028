#pragma once

#include "OrderBook.hpp"
#include <array>
#include <unordered_map>

class OrderBookArray {
public:
    OrderBookArray();
    ~OrderBookArray() = default;

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

    int tick_to_index(PriceTick tick) const;
    PriceTick index_to_tick(int idx) const;

private:
    struct OrderRecord {
        Side side;
        int idx;
        long qty;
    };

    std::unordered_map<uint64_t, OrderRecord> orders_;
    std::array<long, 10001> level_qty_{};
    int best_bid_idx_;
    int best_ask_idx_;
};
