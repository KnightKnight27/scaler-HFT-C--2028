#pragma once

#include "Types.hpp"
#include <vector>
#include <unordered_map>

class OrderBookB : public OrderBook {
public:
    OrderBookB() = default;
    ~OrderBookB() override = default;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    struct OrderRecord {
        Side side;
        double price;
        long qty;
    };

    // Flat sorted vectors of price levels
    // bids_: sorted descending (bids_.front() is best bid)
    std::vector<Level> bids_;
    // asks_: sorted ascending (asks_.front() is best ask)
    std::vector<Level> asks_;

    // Orders mapped by ID for cancellation lookup
    std::unordered_map<uint64_t, OrderRecord> orders_;
};
