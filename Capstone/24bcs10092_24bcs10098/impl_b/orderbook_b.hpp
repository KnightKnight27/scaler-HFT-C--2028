#pragma once

#include "order_book.hpp"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace orderbook {
struct OrderBookBTestAccess;

class OrderBookB final : public OrderBook {
public:
    OrderBookB() = default;
    OrderBookB(const OrderBookB&) = delete;
    OrderBookB& operator=(const OrderBookB&) = delete;
    OrderBookB(OrderBookB&&) = delete;
    OrderBookB& operator=(OrderBookB&&) = delete;

    AddResult add_order(const Order& order) override;
    CancelResult cancel_order(OrderId id) override;
    Bbo get_bbo() const override;
    DepthSnapshot get_bids(Depth depth) const override;
    DepthSnapshot get_asks(Depth depth) const override;

private:
    struct Entry {
        Order order;
        std::uint64_t seq;
    };
    struct Locator {
        Side side;
        std::size_t pos;
    };
    using Heap = std::vector<Entry>;
    using Totals = std::unordered_map<PriceTicks, Quantity>;

    Heap bids_;
    Heap asks_;
    Totals bid_totals_;
    Totals ask_totals_;
    std::unordered_map<OrderId, Locator> live_;
    std::unordered_set<OrderId> used_ids_;
    std::uint64_t next_seq_ = 0;

    static bool better(const Entry& a, const Entry& b, Side side) noexcept;
    void swap_entries(Heap& heap, std::size_t a, std::size_t b) noexcept;
    void sift_up(Heap& heap, std::size_t pos, Side side) noexcept;
    void sift_down(Heap& heap, std::size_t pos, Side side) noexcept;
    static DepthSnapshot depth(const Totals& totals, Depth count, Side side);

    friend struct OrderBookBTestAccess;
};
} // namespace orderbook
