#ifndef ORDERBOOK_B_H
#define ORDERBOOK_B_H

#include "../order.h"
#include <queue>
#include <vector>
#include <unordered_map>

// Implementation B: two priority queues (max-heap for bids, min-heap for asks)
// over price-level objects. A price level holds the total qty at that price.
// Cancels use lazy deletion: we only subtract the qty and leave the level in
// the heap; it gets thrown away later when it floats to the top with qty 0.
class OrderBookB : public OrderBook {
public:
    struct PLevel {
        long tick;
        long qty;
    };

    struct BidBetter {
        bool operator()(const PLevel* a, const PLevel* b) const { return a->tick < b->tick; }
    };
    struct AskBetter {
        bool operator()(const PLevel* a, const PLevel* b) const { return a->tick > b->tick; }
    };

    OrderBookB();
    ~OrderBookB();
    OrderBookB(const OrderBookB&) = delete;             // holds raw pointers
    OrderBookB& operator=(const OrderBookB&) = delete;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    void drop_zeros() const;

    mutable std::priority_queue<PLevel*, std::vector<PLevel*>, BidBetter> bid_heap;
    mutable std::priority_queue<PLevel*, std::vector<PLevel*>, AskBetter> ask_heap;
    mutable std::unordered_map<long, PLevel*> level_by_tick;         // tick -> level
    std::unordered_map<uint64_t, std::pair<PLevel*, long>> orders;   // id -> (level, qty)
};

#endif
