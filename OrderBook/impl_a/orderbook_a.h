#ifndef ORDERBOOK_A_H
#define ORDERBOOK_A_H

#include "../order.h"
#include <map>
#include <functional>
#include <unordered_map>

// Implementation A: std::map for the price levels (sorted tree) and an
// unordered_map to find an order by its id when it is cancelled.
class OrderBookA : public OrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    struct Info {
        Side side;
        long tick;
        long qty;
    };

    // bids highest first, asks lowest first
    std::map<long, long, std::greater<long>> bid_levels;
    std::map<long, long, std::less<long>> ask_levels;
    std::unordered_map<uint64_t, Info> orders;
};

#endif
