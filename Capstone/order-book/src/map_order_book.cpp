#include "map_order_book.hpp"

#include <algorithm>

namespace {

// Takes qty off a level and removes the level once nothing is left at that price.
template <class LevelMap>
void reduce_level(LevelMap& levels, Tick tick, long qty) {
    const auto it = levels.find(tick);  // exists, because a live order rests on it
    it->second -= qty;
    if (it->second == 0) levels.erase(it);
}

// The maps are already in best-first order, so the top levels are the first entries.
template <class LevelMap>
std::vector<Level> top_levels(const LevelMap& levels, int depth) {
    std::vector<Level> out;
    if (depth <= 0) return out;
    const std::size_t count = std::min(static_cast<std::size_t>(depth), levels.size());
    out.reserve(count);
    auto it = levels.begin();
    for (std::size_t i = 0; i < count; ++i, ++it) {
        out.push_back({tick_to_price(it->first), it->second});
    }
    return out;
}

}  // namespace

MapOrderBook::MapOrderBook(std::size_t expected_orders) {
    orders_.reserve(expected_orders);  // sizes the bucket array; nodes are still allocated one by one
}

bool MapOrderBook::add_order(std::uint64_t id, Side side, double price, long qty) {
    Tick tick = 0;
    if (!validate_order(side, price, qty, tick)) return false;
    if (!orders_.try_emplace(id, Order{tick, side, qty}).second) return false;  // id already live

    // operator[] creates the level with quantity 0 the first time a price is used.
    if (side == Side::Buy) {
        bids_[tick] += qty;
    } else {
        asks_[tick] += qty;
    }
    return true;
}

bool MapOrderBook::cancel_order(std::uint64_t id) {
    const auto it = orders_.find(id);
    if (it == orders_.end()) return false;

    const Order& order = it->second;
    if (order.side == Side::Buy) {
        reduce_level(bids_, order.tick, order.qty);
    } else {
        reduce_level(asks_, order.tick, order.qty);
    }
    orders_.erase(it);
    return true;
}

BBO MapOrderBook::get_bbo() const {
    BBO bbo{};
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(bids_.begin()->first);
        bbo.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(asks_.begin()->first);
        bbo.ask_qty = asks_.begin()->second;
    }
    return bbo;
}

std::vector<Level> MapOrderBook::get_bids(int depth) const { return top_levels(bids_, depth); }

std::vector<Level> MapOrderBook::get_asks(int depth) const { return top_levels(asks_, depth); }
