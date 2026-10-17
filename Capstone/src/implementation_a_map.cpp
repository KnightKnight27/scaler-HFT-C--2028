#include "implementation_a_map.hpp"

#include <algorithm>
#include <stdexcept>

namespace {
void check_depth(int depth) {
    if (depth < 0) throw std::invalid_argument("depth cannot be negative");
}
} // namespace

void MapOrderBook::add_order(std::uint64_t id, Side side, double price, long qty) {
    if (qty <= 0) throw std::invalid_argument("quantity must be positive");
    const int ticks = price_to_ticks(price, side);
    if (!orders_.emplace(id, Order{side, ticks, qty}).second) {
        throw std::invalid_argument("duplicate order ID");
    }
    if (side == Side::Buy) bids_[ticks] += qty;
    else asks_[ticks] += qty;
}

bool MapOrderBook::cancel_order(std::uint64_t id) {
    const auto order_it = orders_.find(id);
    if (order_it == orders_.end()) return false;
    const Order order = order_it->second;
    if (order.side == Side::Buy) {
        auto level = bids_.find(order.price);
        level->second -= order.qty;
        if (level->second == 0) bids_.erase(level);
    } else {
        auto level = asks_.find(order.price);
        level->second -= order.qty;
        if (level->second == 0) asks_.erase(level);
    }
    orders_.erase(order_it);
    return true;
}

BBO MapOrderBook::get_bbo() const {
    BBO out;
    if (!bids_.empty()) {
        out.has_bid = true;
        out.bid_price = ticks_to_price(bids_.begin()->first);
        out.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        out.has_ask = true;
        out.ask_price = ticks_to_price(asks_.begin()->first);
        out.ask_qty = asks_.begin()->second;
    }
    return out;
}

std::vector<Level> MapOrderBook::get_bids(int depth) const {
    check_depth(depth);
    std::vector<Level> result;
    result.reserve(std::min<std::size_t>(static_cast<std::size_t>(depth), bids_.size()));
    for (auto it = bids_.begin(); it != bids_.end() && static_cast<int>(result.size()) < depth; ++it)
        result.push_back({ticks_to_price(it->first), it->second});
    return result;
}

std::vector<Level> MapOrderBook::get_asks(int depth) const {
    check_depth(depth);
    std::vector<Level> result;
    result.reserve(std::min<std::size_t>(static_cast<std::size_t>(depth), asks_.size()));
    for (auto it = asks_.begin(); it != asks_.end() && static_cast<int>(result.size()) < depth; ++it)
        result.push_back({ticks_to_price(it->first), it->second});
    return result;
}
