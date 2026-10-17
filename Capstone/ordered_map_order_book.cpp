// Data structure: ordered std::map price levels with an std::unordered_map ID index.
#include "ordered_map_order_book.hpp"

#include <cstddef>
#include <stdexcept>

namespace orderbook {
namespace {

template <typename Map>
void subtract_level(Map& levels, PriceTicks price_ticks, long qty) {
    auto it = levels.find(price_ticks);
    it->second -= qty;
    if (it->second == 0) {
        levels.erase(it);
    }
}

}  // namespace

void OrderedMapOrderBook::add_order(std::uint64_t id, Side side,
                                    PriceTicks price_ticks, long qty) {
    validate_order(side, price_ticks, qty);
    const auto inserted = orders_.emplace(id, Order{side, price_ticks, qty});
    if (!inserted.second) {
        throw std::invalid_argument("an order with this ID has already been used");
    }
    if (side == Side::Buy) {
        bids_[price_ticks] += qty;
    } else {
        asks_[price_ticks] += qty;
    }
}

bool OrderedMapOrderBook::cancel_order(std::uint64_t id) {
    const auto order = orders_.find(id);
    if (order == orders_.end() || !order->second.live) {
        return false;
    }
    if (order->second.side == Side::Buy) {
        subtract_level(bids_, order->second.price_ticks, order->second.qty);
    } else {
        subtract_level(asks_, order->second.price_ticks, order->second.qty);
    }
    order->second.live = false;
    return true;
}

BBO OrderedMapOrderBook::get_bbo() const {
    BBO bbo;
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price_ticks = bids_.begin()->first;
        bbo.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price_ticks = asks_.begin()->first;
        bbo.ask_qty = asks_.begin()->second;
    }
    return bbo;
}

std::vector<Level> OrderedMapOrderBook::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    result.reserve(static_cast<std::size_t>(depth));
    for (auto it = bids_.begin();
         it != bids_.end() && result.size() < static_cast<std::size_t>(depth); ++it) {
        result.push_back({it->first, it->second});
    }
    return result;
}

std::vector<Level> OrderedMapOrderBook::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    result.reserve(static_cast<std::size_t>(depth));
    for (auto it = asks_.begin();
         it != asks_.end() && result.size() < static_cast<std::size_t>(depth); ++it) {
        result.push_back({it->first, it->second});
    }
    return result;
}

} 
