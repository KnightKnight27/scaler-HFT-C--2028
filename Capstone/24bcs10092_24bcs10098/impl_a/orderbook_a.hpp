#pragma once

#include "order_book.hpp"

#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace orderbook {

struct OrderBookATestAccess;

class OrderBookA final : public OrderBook {
public:
    OrderBookA() = default;
    // Locators refer to this instance's map and list nodes.
    OrderBookA(const OrderBookA&) = delete;
    OrderBookA& operator=(const OrderBookA&) = delete;
    OrderBookA(OrderBookA&&) = delete;
    OrderBookA& operator=(OrderBookA&&) = delete;

    AddResult add_order(const Order& order) override;
    CancelResult cancel_order(OrderId id) override;
    Bbo get_bbo() const override;
    DepthSnapshot get_bids(Depth depth) const override;
    DepthSnapshot get_asks(Depth depth) const override;

private:
    struct Level {
        Quantity total = 0;
        std::list<Order> orders;
    };
    using Levels = std::map<PriceTicks, Level>;
    struct Locator {
        Side side;
        Levels::iterator level;
        std::list<Order>::iterator order;
    };

    // Both maps ascend; bid queries traverse in reverse.
    Levels bids_;
    Levels asks_;
    std::unordered_map<OrderId, Locator> live_;
    std::unordered_set<OrderId> used_ids_;

    // Verification-only access; no extra operation in the public book API.
    friend struct OrderBookATestAccess;
};

} // namespace orderbook
