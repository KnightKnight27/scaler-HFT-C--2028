#pragma once

#include "types.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace ob {

// A: ordered trees for both IDs and price levels.
class MapBook {
public:
    void add_order(OrderId id, Side side, Price price, Quantity qty) {
        validate_order(side, price, qty);
        const auto next_order = orders_.lower_bound(id);
        if (next_order != orders_.end() && next_order->first == id) {
            throw std::invalid_argument("duplicate live order ID");
        }
        auto& levels = side == Side::Buy ? bids_ : asks_;
        const auto level = levels.lower_bound(price);
        const bool exists = level != levels.end() && level->first == price;
        check_quantity_sum(exists ? level->second : 0, qty);
        // Both lower_bound results are also insertion hints; do not search twice.
        const auto order = orders_.emplace_hint(next_order, id, Order{side, price, qty});
        // Roll back the ID insertion if allocating a new price node fails.
        try {
            if (exists) level->second += qty;
            else levels.emplace_hint(level, price, qty);
        } catch (...) {
            orders_.erase(order);
            throw;
        }
    }

    bool cancel_order(OrderId id) {
        auto order = orders_.find(id);
        if (order == orders_.end()) {
            return false;
        }
        const Order& value = order->second;
        auto& levels = value.side == Side::Buy ? bids_ : asks_;
        auto level = levels.find(value.price);
        level->second -= value.qty;
        if (level->second == 0) {
            levels.erase(level);
        }
        orders_.erase(order);
        return true;
    }

    BBO get_bbo() const {
        BBO result;
        if (!bids_.empty()) {
            const auto& best = *bids_.rbegin();
            result.has_bid = true;
            result.bid_price = best.first;
            result.bid_qty = best.second;
        }
        if (!asks_.empty()) {
            const auto& best = *asks_.begin();
            result.has_ask = true;
            result.ask_price = best.first;
            result.ask_qty = best.second;
        }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        const auto count = std::min(bids_.size(), static_cast<std::size_t>(depth));
        result.reserve(count);
        for (auto it = bids_.rbegin(); it != bids_.rend() && result.size() < count; ++it) {
            result.push_back({it->first, it->second});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        const auto count = std::min(asks_.size(), static_cast<std::size_t>(depth));
        result.reserve(count);
        for (auto it = asks_.begin(); it != asks_.end() && result.size() < count; ++it) {
            result.push_back({it->first, it->second});
        }
        return result;
    }

private:
    std::map<OrderId, Order> orders_;
    std::map<Price, Quantity> bids_;
    std::map<Price, Quantity> asks_;
};

} // namespace ob

