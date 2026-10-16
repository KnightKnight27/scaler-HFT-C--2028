#pragma once

#include "Types.hpp"
#include <map>
#include <unordered_map>

class OrderBookA : public OrderBook {
public:
    OrderBookA() = default;
    ~OrderBookA() override = default;

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

    // Price levels stored in standard Red-Black Trees (std::map)
    // Bids sorted descending (highest bid first)
    std::map<double, long, std::greater<double>> bids_;
    // Asks sorted ascending (lowest ask first)
    std::map<double, long, std::less<double>> asks_;

    // Orders mapped by ID for cancellation lookup
    std::unordered_map<uint64_t, OrderRecord> orders_;
};
