#ifndef ORDERBOOK_C_H
#define ORDERBOOK_C_H

#include "../order.h"
#include <vector>
#include <unordered_map>

// Implementation C (own design): a fixed array indexed directly by the price
// tick. There are only 10,000 possible prices so a plain array of quantities is
// enough. add/cancel are O(1) array bumps; get_bbo scans outward from the edge
// of the band until it hits a level that has qty.
class OrderBookC : public OrderBook {
public:
    OrderBookC();

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

    // tick 5000..9999  -> bids, index = tick - 5000
    // tick 10001..15000 -> asks, index = tick - 10001
    static const int BID_LOW = 5000;
    static const int ASK_LOW = 10001;
    static const int BAND = 5000;

    std::vector<long> bid_qty;
    std::vector<long> ask_qty;
    std::unordered_map<uint64_t, Info> orders;
};

#endif
