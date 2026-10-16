#include "OrderBookA.hpp"
#include <algorithm>

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty) {
    price = normalize_price(price);
    orders_[id] = {side, price, qty};
    if (side == Side::Buy) {
        bids_[price] += qty;
    } else {
        asks_[price] += qty;
    }
}

bool OrderBookA::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }

    const auto& order = it->second;
    if (order.side == Side::Buy) {
        auto map_it = bids_.find(order.price);
        if (map_it != bids_.end()) {
            map_it->second -= order.qty;
            if (map_it->second <= 0) {
                bids_.erase(map_it);
            }
        }
    } else {
        auto map_it = asks_.find(order.price);
        if (map_it != asks_.end()) {
            map_it->second -= order.qty;
            if (map_it->second <= 0) {
                asks_.erase(map_it);
            }
        }
    }

    orders_.erase(it);
    return true;
}

BBO OrderBookA::get_bbo() const {
    BBO bbo{};
    if (!bids_.empty()) {
        auto it = bids_.begin();
        bbo.has_bid = true;
        bbo.bid_price = it->first;
        bbo.bid_qty = it->second;
    } else {
        bbo.has_bid = false;
        bbo.bid_price = 0.0;
        bbo.bid_qty = 0;
    }

    if (!asks_.empty()) {
        auto it = asks_.begin();
        bbo.has_ask = true;
        bbo.ask_price = it->first;
        bbo.ask_qty = it->second;
    } else {
        bbo.has_ask = false;
        bbo.ask_price = 0.0;
        bbo.ask_qty = 0;
    }
    return bbo;
}

std::vector<Level> OrderBookA::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min(static_cast<size_t>(depth), bids_.size()));

    for (auto it = bids_.begin(); it != bids_.end() && static_cast<int>(result.size()) < depth; ++it) {
        result.push_back({it->first, it->second});
    }
    return result;
}

std::vector<Level> OrderBookA::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min(static_cast<size_t>(depth), asks_.size()));

    for (auto it = asks_.begin(); it != asks_.end() && static_cast<int>(result.size()) < depth; ++it) {
        result.push_back({it->first, it->second});
    }
    return result;
}
