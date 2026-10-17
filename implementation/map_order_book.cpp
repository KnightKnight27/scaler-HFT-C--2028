#include "map_order_book.hpp"

#include <algorithm>

void MapOrderBook::add_order(std::uint64_t id, Side side, int price_ticks, long qty) {
    orders_.emplace(id, Order{side, price_ticks, qty});
    auto &levels = side == Side::Buy ? bids_ : asks_;
    levels[price_ticks] += qty;
}

bool MapOrderBook::cancel_order(std::uint64_t id) {
    const auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }
    const Order order = it->second;
    auto &levels = order.side == Side::Buy ? bids_ : asks_;
    const auto level = levels.find(order.price_ticks);
    level->second -= order.qty;
    if (level->second == 0) {
        levels.erase(level);
    }
    orders_.erase(it);
    return true;
}

BBO MapOrderBook::get_bbo() const {
    BBO bbo;
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price_ticks = bids_.rbegin()->first;
        bbo.bid_qty = bids_.rbegin()->second;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price_ticks = asks_.begin()->first;
        bbo.ask_qty = asks_.begin()->second;
    }
    return bbo;
}

std::vector<Level> MapOrderBook::get_bids(int depth) const {
    std::vector<Level> out;
    if (depth <= 0) {
        return out;
    }
    out.reserve(std::min<std::size_t>(bids_.size(), static_cast<std::size_t>(depth)));
    for (auto it = bids_.rbegin();
         it != bids_.rend() && out.size() < static_cast<std::size_t>(depth); ++it) {
        out.push_back({it->first, it->second});
    }
    return out;
}

std::vector<Level> MapOrderBook::get_asks(int depth) const {
    std::vector<Level> out;
    if (depth <= 0) {
        return out;
    }
    out.reserve(std::min<std::size_t>(asks_.size(), static_cast<std::size_t>(depth)));
    for (auto it = asks_.begin(); it != asks_.end() && out.size() < static_cast<std::size_t>(depth);
         ++it) {
        out.push_back({it->first, it->second});
    }
    return out;
}
