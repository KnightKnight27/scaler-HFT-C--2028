#include "book_map.hpp"

#include <algorithm>

namespace {

template <class LevelMap>
void reduce_level(LevelMap& levels, int32_t tick, long qty) {
    auto it = levels.find(tick);
    it->second -= qty;
    if (it->second == 0) levels.erase(it);
}

template <class LevelMap>
std::vector<Level> top_levels(const LevelMap& levels, int depth) {
    std::vector<Level> out;
    if (depth <= 0) return out;
    out.reserve(std::min<size_t>(static_cast<size_t>(depth), levels.size()));
    for (auto it = levels.begin(); it != levels.end() && static_cast<int>(out.size()) < depth; ++it)
        out.push_back({px::to_price(it->first), it->second});
    return out;
}

}  // namespace

bool OrderBookMap::add_order(uint64_t id, Side side, double price, long qty) {
    int32_t tick;
    if (!px::validate_order(side, price, qty, tick)) return false;
    if (!orders_.try_emplace(id, Order{tick, side, qty}).second) return false;  // duplicate live id

    if (side == Side::Buy)
        bids_[tick] += qty;
    else
        asks_[tick] += qty;
    return true;
}

bool OrderBookMap::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return false;

    const Order& o = it->second;
    if (o.side == Side::Buy)
        reduce_level(bids_, o.tick, o.qty);
    else
        reduce_level(asks_, o.tick, o.qty);
    orders_.erase(it);
    return true;
}

BBO OrderBookMap::get_bbo() const {
    BBO b{};
    if (!bids_.empty()) {
        b.has_bid = true;
        b.bid_price = px::to_price(bids_.begin()->first);
        b.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        b.has_ask = true;
        b.ask_price = px::to_price(asks_.begin()->first);
        b.ask_qty = asks_.begin()->second;
    }
    return b;
}

std::vector<Level> OrderBookMap::get_bids(int depth) const { return top_levels(bids_, depth); }
std::vector<Level> OrderBookMap::get_asks(int depth) const { return top_levels(asks_, depth); }
