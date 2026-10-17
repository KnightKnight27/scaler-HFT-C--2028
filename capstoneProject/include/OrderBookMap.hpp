#pragma once

#include "OrderBook.hpp"
#include <map>
#include <unordered_map>

class OrderBookMap {
public:
    OrderBookMap() = default;
    ~OrderBookMap() = default;

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct OrderRecord {
        Side side;
        PriceTick tick;
        long qty;
    };

    std::unordered_map<uint64_t, OrderRecord> orders_;
    std::map<PriceTick, long, std::greater<PriceTick>> bids_;
    std::map<PriceTick, long, std::less<PriceTick>> asks_;
};
