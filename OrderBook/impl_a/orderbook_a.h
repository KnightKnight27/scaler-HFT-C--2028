#ifndef ORDERBOOK_A_H
#define ORDERBOOK_A_H

#include "order.h"
#include <map>
#include <unordered_map>
#include <vector>

/**
 * Implementation A:
 * - bids: std::map<long, long, std::greater<long>> (tick price -> total quantity)
 * - asks: std::map<long, long, std::less<long>> (tick price -> total quantity)
 * - orders: std::unordered_map<uint64_t, OrderInfo>
 */
class OrderBookA : public IOrderBook {
public:
    OrderBookA() = default;
    ~OrderBookA() override = default;

    OrderBookA(const OrderBookA&) = default;
    OrderBookA& operator=(const OrderBookA&) = default;
    OrderBookA(OrderBookA&&) noexcept = default;
    OrderBookA& operator=(OrderBookA&&) noexcept = default;

    void add_order(uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(uint64_t id) override;
    BBO get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    struct OrderInfo {
        Side side;
        long price; // in ticks (price * 100)
        long qty;
    };

    std::map<long, long, std::greater<long>> m_bids;
    std::map<long, long, std::less<long>> m_asks;
    std::unordered_map<uint64_t, OrderInfo> m_orders;
};

#endif // ORDERBOOK_A_H
