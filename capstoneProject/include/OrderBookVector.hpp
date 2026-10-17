#pragma once

#include "OrderBook.hpp"
#include <vector>
#include <unordered_map>

class OrderBookVector {
public:
    OrderBookVector() = default;
    ~OrderBookVector() = default;

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct LevelTick {
        PriceTick tick;
        long qty;
    };

    struct OrderRecord {
        Side side;
        PriceTick tick;
        long qty;
    };

    std::unordered_map<uint64_t, OrderRecord> orders_;
    std::vector<LevelTick> bids_;
    std::vector<LevelTick> asks_;
};
