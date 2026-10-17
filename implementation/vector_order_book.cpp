#include "vector_order_book.hpp"
#include <algorithm>

namespace {
auto find_level(std::vector<Level> &levels, int price) {
    return std::lower_bound(levels.begin(), levels.end(), price,
                            [](const Level &level, int key) { return level.price_ticks < key; });
}
} // namespace

void VectorOrderBook::add_order(std::uint64_t id, Side side, int price_ticks, long qty) {
    orders_.emplace(id, Order{side, price_ticks, qty});
    auto &levels = side == Side::Buy ? bids_ : asks_;
    auto it = find_level(levels, price_ticks);
    if (it != levels.end() && it->price_ticks == price_ticks) {
        it->qty += qty;
    } else {
        levels.insert(it, Level{price_ticks, qty});
    }
}

bool VectorOrderBook::cancel_order(std::uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }
    const Order order = it->second;
    auto &levels = order.side == Side::Buy ? bids_ : asks_;
    auto level = find_level(levels, order.price_ticks);
    level->qty -= order.qty;
    if (level->qty == 0) {
        levels.erase(level);
    }
    orders_.erase(it);
    return true;
}

BBO VectorOrderBook::get_bbo() const {
    BBO bbo;
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price_ticks = bids_.back().price_ticks;
        bbo.bid_qty = bids_.back().qty;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price_ticks = asks_.front().price_ticks;
        bbo.ask_qty = asks_.front().qty;
    }
    return bbo;
}

std::vector<Level> VectorOrderBook::get_bids(int depth) const {
    if (depth <= 0) {
        return {};
    }
    const auto count = std::min(bids_.size(), static_cast<std::size_t>(depth));
    return {bids_.rbegin(), bids_.rbegin() + static_cast<std::ptrdiff_t>(count)};
}

std::vector<Level> VectorOrderBook::get_asks(int depth) const {
    if (depth <= 0) {
        return {};
    }
    const auto count = std::min(asks_.size(), static_cast<std::size_t>(depth));
    return {asks_.begin(), asks_.begin() + static_cast<std::ptrdiff_t>(count)};
}
