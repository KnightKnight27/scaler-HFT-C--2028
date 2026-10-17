#pragma once

#include "order_book.hpp"

#include <array>
#include <cstddef>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace orderbook {
struct OrderBookCTestAccess;

class OrderBookC final : public OrderBook {
public:
    OrderBookC() = default;
    OrderBookC(const OrderBookC&) = delete;
    OrderBookC& operator=(const OrderBookC&) = delete;
    OrderBookC(OrderBookC&&) = delete;
    OrderBookC& operator=(OrderBookC&&) = delete;

    AddResult add_order(const Order& order) override;
    CancelResult cancel_order(OrderId id) override;
    Bbo get_bbo() const override;
    DepthSnapshot get_bids(Depth count) const override;
    DepthSnapshot get_asks(Depth count) const override;

private:
    static constexpr std::size_t level_count = 5000;
    static constexpr std::size_t word_count = (level_count + 63) / 64;
    static constexpr std::uint64_t last_mask =
        (std::uint64_t{1} << (level_count % 64)) - 1;
    struct Level {
        Quantity total = 0;
        std::list<Order> orders;
    };
    using Levels = std::array<Level, level_count>;
    using Bitmap = std::array<std::uint64_t, word_count>;
    struct Locator {
        Side side;
        std::size_t index;
        std::list<Order>::iterator order;
    };

    Levels bids_{};
    Levels asks_{};
    Bitmap bid_bits_{};
    Bitmap ask_bits_{};
    std::unordered_map<OrderId, Locator> live_;
    std::unordered_set<OrderId> used_ids_;

    // Called only after side/price validation.
    static std::size_t index_of(Side side, PriceTicks price) noexcept;
    static PriceTicks price_of(Side side, std::size_t index) noexcept;
    // word must be nonzero. Uses portable C++17 shifts, no compiler intrinsics.
    static unsigned bit_index(std::uint64_t word, bool highest) noexcept;
    static std::optional<std::size_t> best_index(const Bitmap& bits, Side side) noexcept;
    DepthSnapshot depth(Side side, Depth count) const;

    friend struct OrderBookCTestAccess;
};
} // namespace orderbook
