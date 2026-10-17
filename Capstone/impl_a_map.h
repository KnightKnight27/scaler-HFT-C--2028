#ifndef IMPL_A_MAP_H
#define IMPL_A_MAP_H

#include "orderbook.h"
#include <map>
#include <unordered_map>
#include <functional>

class OrderBookA : public IOrderBook {
private:
    std::map<int, long, std::greater<int>> bids;
    std::map<int, long> asks;
    std::unordered_map<uint64_t, OrderRec> orders;

public:
    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth) const override;
    std::vector<Level> get_asks(int depth) const override;
};

#endif
