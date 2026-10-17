#ifndef ORDERBOOK_B_H
#define ORDERBOOK_B_H

#include "order.h"
#include <queue>
#include <unordered_map>
#include <vector>

/**
 * Implementation B:
 * - bids: std::priority_queue<Order*, ...>
 * - asks: std::priority_queue<Order*, ...>
 * - orders: std::unordered_map<uint64_t, std::pair<Order*, long>>
 * - priceOrderMap: std::unordered_map<long, Order*> (tick price -> Order*)
 *
 * Lazy deletion: Orders with qty == 0 are popped, freed, and removed from
 * priceOrderMap upon retrieval in get_bbo, get_bids, or get_asks.
 */
class OrderBookB : public IOrderBook {
public:
    struct Order {
        long price; // in ticks (price * 100)
        long qty;
        Side side;
    };

    struct BidCompare {
        bool operator()(const Order* a, const Order* b) const {
            return a->price < b->price; // Max-heap: highest price at top
        }
    };

    struct AskCompare {
        bool operator()(const Order* a, const Order* b) const {
            return a->price > b->price; // Min-heap: lowest price at top
        }
    };

    OrderBookB();
    ~OrderBookB() override;

    OrderBookB(const OrderBookB&) = delete;
    OrderBookB& operator=(const OrderBookB&) = delete;

    OrderBookB(OrderBookB&& other) noexcept;
    OrderBookB& operator=(OrderBookB&& other) noexcept;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    void clear();

    mutable std::priority_queue<Order*, std::vector<Order*>, BidCompare> m_bids;
    mutable std::priority_queue<Order*, std::vector<Order*>, AskCompare> m_asks;
    std::unordered_map<uint64_t, std::pair<Order*, long>> m_orders;
    mutable std::unordered_map<long, Order*> m_priceOrderMap;
};

#endif // ORDERBOOK_B_H
