#ifndef IMPL_B_PQUEUE_H
#define IMPL_B_PQUEUE_H

#include "orderbook.h"
#include <queue>
#include <unordered_map>
#include <functional>

class OrderBookB : public IOrderBook {
private:
    std::unordered_map<uint64_t, OrderRec> orders;
    std::unordered_map<int, long> bidQty;
    std::unordered_map<int, long> askQty;
    mutable std::priority_queue<int> bidHeap;
    mutable std::priority_queue<int, std::vector<int>, std::greater<int>> askHeap;

    void clean_bids() const;
    void clean_asks() const;

public:
    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth) const override;
    std::vector<Level> get_asks(int depth) const override;
};

#endif
