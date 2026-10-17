// Data structure: price-priority sorted std::vector levels with an std::unordered_map ID index.
#include "sorted_vector_order_book.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace orderbook {
namespace {

template <typename Levels, typename Compare>
auto find_level(Levels& levels, PriceTicks price_ticks, Compare compare) {
    return std::lower_bound(
        levels.begin(), levels.end(), price_ticks,
        [compare](const Level& level, PriceTicks price) {
            return compare(level.price_ticks, price);
        });
}

auto price_order(Side side) {
    return [side](PriceTicks lhs, PriceTicks rhs) {
        return side == Side::Buy ? lhs > rhs : lhs < rhs;
    };
}

}  // namespace

void SortedVectorOrderBook::add_order(std::uint64_t id, Side side,
                                      PriceTicks price_ticks, long qty) {
    validate_order(side, price_ticks, qty);
    const auto inserted = orders_.emplace(id, Order{side, price_ticks, qty});
    if (!inserted.second) {
        throw std::invalid_argument("an order with this ID has already been used");
    }

    auto& levels = side == Side::Buy ? bids_ : asks_;
    const auto it = find_level(levels, price_ticks, price_order(side));
    if (it != levels.end() && it->price_ticks == price_ticks) {
        it->qty += qty;
    } else {
        levels.insert(it, Level{price_ticks, qty});
    }
}

bool SortedVectorOrderBook::cancel_order(std::uint64_t id) {
    const auto order = orders_.find(id);
    if (order == orders_.end() || !order->second.live) {
        return false;
    }
    const auto side = order->second.side;
    auto& levels = side == Side::Buy ? bids_ : asks_;
    const auto it =
        find_level(levels, order->second.price_ticks, price_order(side));
    it->qty -= order->second.qty;
    if (it->qty == 0) {
        levels.erase(it);
    }
    order->second.live = false;
    return true;
}

BBO SortedVectorOrderBook::get_bbo() const {
    BBO bbo;
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price_ticks = bids_.front().price_ticks;
        bbo.bid_qty = bids_.front().qty;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price_ticks = asks_.front().price_ticks;
        bbo.ask_qty = asks_.front().qty;
    }
    return bbo;
}

std::vector<Level> SortedVectorOrderBook::get_bids(int depth) const {
    const auto count =
        std::min(bids_.size(), depth <= 0 ? std::size_t{0}
                                         : static_cast<std::size_t>(depth));
    return {bids_.begin(), bids_.begin() + static_cast<std::ptrdiff_t>(count)};
}

std::vector<Level> SortedVectorOrderBook::get_asks(int depth) const {
    const auto count =
        std::min(asks_.size(), depth <= 0 ? std::size_t{0}
                                         : static_cast<std::size_t>(depth));
    return {asks_.begin(), asks_.begin() + static_cast<std::ptrdiff_t>(count)};
}

}  // namespace orderbook
