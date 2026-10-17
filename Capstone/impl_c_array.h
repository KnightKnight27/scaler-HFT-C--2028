#ifndef IMPL_C_ARRAY_H
#define IMPL_C_ARRAY_H

#include "orderbook.h"
#include <vector>

struct OrderRec2 {
    Side side;
    int tick;
    long qty;
    bool live;
};

class OrderBookC : public IOrderBook {
private:
    std::vector<long> bidQty;
    std::vector<long> askQty;
    std::vector<OrderRec2> orders;
    int bestBidIdx;
    int bestAskIdx;

    static constexpr int BID_SLOTS = BID_MAX_TICK - BID_MIN_TICK + 1;
    static constexpr int ASK_SLOTS = ASK_MAX_TICK - ASK_MIN_TICK + 1;

public:
    OrderBookC();
    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth) const override;
    std::vector<Level> get_asks(int depth) const override;
};

#endif
