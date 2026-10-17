// Data structure: fixed price-tick indexed arrays plus std::unordered_map order-ID index.
#pragma once

#include "order_book.hpp"

#include <array>
#include <cstddef>
#include <unordered_map>

namespace orderbook {

class FixedArrayOrderBook {
public:
    void add_order(std::uint64_t id, Side side, PriceTicks price_ticks, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct Order {
        Side side;
        PriceTicks price_ticks;
        long qty;
        bool live = true;
    };

    static constexpr std::size_t kMaxPriceTicks = 15000;
    std::array<long, kMaxPriceTicks + 1> bid_qty_{};
    std::array<long, kMaxPriceTicks + 1> ask_qty_{};
    PriceTicks best_bid_ticks_ = -1;
    PriceTicks best_ask_ticks_ = -1;
    std::unordered_map<std::uint64_t, Order> orders_;
};

}  // namespace orderbook
