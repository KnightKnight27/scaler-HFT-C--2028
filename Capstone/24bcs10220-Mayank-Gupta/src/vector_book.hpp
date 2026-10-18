#pragma once

#include "types.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace ob {

// B: sorted vectors for levels; a hash table holds individual orders.
class VectorBook {
public:
    void add_order(OrderId id, Side side, Price price, Quantity qty) {
        validate_order(side, price, qty);
        if (orders_.find(id) != orders_.end()) {
            throw std::invalid_argument("duplicate live order ID");
        }
        auto& levels = side == Side::Buy ? bids_ : asks_;
        auto level = find_level(levels, price);
        const bool exists = level != levels.end() && level->price == price;
        check_quantity_sum(exists ? level->qty : 0, qty);
        orders_.emplace(id, Order{side, price, qty});
        try {
            if (exists) {
                level->qty += qty;
            } else {
                levels.insert(level, Level{price, qty});
            }
        } catch (...) {
            orders_.erase(id);
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
        auto level = find_level(levels, value.price);
        level->qty -= value.qty;
        if (level->qty == 0) {
            levels.erase(level);
        }
        orders_.erase(order);
        return true;
    }

    BBO get_bbo() const {
        BBO result;
        if (!bids_.empty()) {
            result.has_bid = true;
            result.bid_price = bids_.back().price;
            result.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            result.has_ask = true;
            result.ask_price = asks_.front().price;
            result.ask_qty = asks_.front().qty;
        }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        const auto count = std::min(bids_.size(), static_cast<std::size_t>(depth));
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            result.push_back(bids_[bids_.size() - 1 - i]);
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        if (depth <= 0) return {};
        const auto count = std::min(asks_.size(), static_cast<std::size_t>(depth));
        return {asks_.begin(), asks_.begin() + static_cast<std::ptrdiff_t>(count)};
    }

private:
    static std::vector<Level>::iterator find_level(std::vector<Level>& levels, Price price) {
        return std::lower_bound(levels.begin(), levels.end(), price,
            [](const Level& level, Price value) { return level.price < value; });
    }

    std::unordered_map<OrderId, Order> orders_;
    std::vector<Level> bids_; // Both sides sorted in ascending price order.
    std::vector<Level> asks_;
};

} // namespace ob
