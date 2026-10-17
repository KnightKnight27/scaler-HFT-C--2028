#pragma once

#include "order.hpp"

namespace orderbook {

// Common interface for all implementations.
// See implementations.md for the behavior of each operation.
// An accepted ID may never be reused during this book instance's lifetime.
class OrderBook {
public:
    virtual ~OrderBook() = default;

    virtual AddResult add_order(const Order& order) = 0;
    virtual CancelResult cancel_order(OrderId id) = 0;
    virtual Bbo get_bbo() const = 0;
    virtual DepthSnapshot get_bids(Depth depth) const = 0;
    virtual DepthSnapshot get_asks(Depth depth) const = 0;
};

} // namespace orderbook
