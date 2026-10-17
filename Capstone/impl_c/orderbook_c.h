#ifndef ORDERBOOK_C_H
#define ORDERBOOK_C_H

#include "order.h"
#include <list>
#include <unordered_map>
#include <vector>

/**
 * Implementation C:
 * - orders: std::unordered_map<uint64_t, Order*>
 * - bids: std::list<Order*> (sorted descending by price)
 * - asks: std::list<Order*> (sorted ascending by price)
 *
 * Each order stores an iterator into the linked list for O(1) removal on cancellation.
 * Price levels are dynamically aggregated on queries.
 */
class OrderBookC : public IOrderBook {
public:
    struct Order {
        uint64_t id;
        Side side;
        long price; // in ticks (price * 100)
        long qty;
        std::list<Order*>::iterator list_it;
    };

    OrderBookC();
    ~OrderBookC() override;

    OrderBookC(const OrderBookC&) = delete;
    OrderBookC& operator=(const OrderBookC&) = delete;

    OrderBookC(OrderBookC&& other) noexcept;
    OrderBookC& operator=(OrderBookC&& other) noexcept;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    void clear();

    std::unordered_map<uint64_t, Order*> m_orders;
    std::list<Order*> m_bids; // sorted descending by price
    std::list<Order*> m_asks; // sorted ascending by price
};

#endif // ORDERBOOK_C_H
