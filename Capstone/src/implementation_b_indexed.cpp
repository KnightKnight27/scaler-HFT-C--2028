#include "implementation_b_indexed.hpp"

#include <algorithm>
#include <stdexcept>

namespace {
void check_depth(int depth) {
    if (depth < 0) throw std::invalid_argument("depth cannot be negative");
}
} // namespace

void IndexedOrderBook::add_order(std::uint64_t id, Side side, double price, long qty) {
    if (qty <= 0) throw std::invalid_argument("quantity must be positive");
    const int ticks = price_to_ticks(price, side);
    if (!orders_.emplace(id, Order{side, ticks, qty}).second)
        throw std::invalid_argument("duplicate order ID");
    if (side == Side::Buy) {
        bid_qty_[ticks] += qty;
        bid_prices_.insert(ticks);
    } else {
        ask_qty_[ticks] += qty;
        ask_prices_.insert(ticks);
    }
}

bool IndexedOrderBook::cancel_order(std::uint64_t id) {
    const auto it = orders_.find(id);
    if (it == orders_.end()) return false;
    const Order order = it->second;
    auto& quantities = order.side == Side::Buy ? bid_qty_ : ask_qty_;
    auto level = quantities.find(order.price);
    level->second -= order.qty;
    if (level->second == 0) {
        quantities.erase(level);
        if (order.side == Side::Buy) bid_prices_.erase(order.price);
        else ask_prices_.erase(order.price);
    }
    orders_.erase(it);
    return true;
}

BBO IndexedOrderBook::get_bbo() const {
    BBO out;
    if (!bid_prices_.empty()) {
        const int price = *bid_prices_.begin();
        out.has_bid = true;
        out.bid_price = ticks_to_price(price);
        out.bid_qty = bid_qty_.at(price);
    }
    if (!ask_prices_.empty()) {
        const int price = *ask_prices_.begin();
        out.has_ask = true;
        out.ask_price = ticks_to_price(price);
        out.ask_qty = ask_qty_.at(price);
    }
    return out;
}

std::vector<Level> IndexedOrderBook::get_bids(int depth) const {
    check_depth(depth);
    std::vector<Level> result;
    result.reserve(std::min<std::size_t>(static_cast<std::size_t>(depth), bid_prices_.size()));
    for (int price : bid_prices_) {
        if (static_cast<int>(result.size()) == depth) break;
        result.push_back({ticks_to_price(price), bid_qty_.at(price)});
    }
    return result;
}

std::vector<Level> IndexedOrderBook::get_asks(int depth) const {
    check_depth(depth);
    std::vector<Level> result;
    result.reserve(std::min<std::size_t>(static_cast<std::size_t>(depth), ask_prices_.size()));
    for (int price : ask_prices_) {
        if (static_cast<int>(result.size()) == depth) break;
        result.push_back({ticks_to_price(price), ask_qty_.at(price)});
    }
    return result;
}
