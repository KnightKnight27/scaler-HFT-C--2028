#pragma once
#include "types.hpp"
#include <map>

namespace ob {
// A: ordered nodes for both order IDs and aggregated price levels.
class TreeBook {
    std::map<std::uint64_t, Order> orders_;
    std::map<Tick, long> bids_, asks_;
    std::map<Tick, long>& levels(Side s) { return s == Side::Buy ? bids_ : asks_; }
public:
    void add_order(std::uint64_t id, Side side, Tick price, long qty) {
        validate(side, price, qty);
        if (orders_.find(id) != orders_.end())
            throw std::invalid_argument("duplicate live order ID");
        auto& side_levels = levels(side);
        auto inserted = side_levels.try_emplace(price, 0);
        auto level = inserted.first;
        try {
            check_sum(level->second, qty);
            orders_.emplace(id, Order{side, price, qty});
        } catch (...) {
            if (inserted.second) side_levels.erase(level);
            throw;
        }
        level->second += qty;
    }
    bool cancel_order(std::uint64_t id) {
        const auto order = orders_.find(id);
        if (order == orders_.end()) return false;
        auto& side_levels = levels(order->second.side);
        auto level = side_levels.find(order->second.price);
        level->second -= order->second.qty;
        if (level->second == 0) side_levels.erase(level);
        orders_.erase(order);
        return true;
    }
    BBO get_bbo() const {
        BBO result;
        if (!bids_.empty()) {
            const auto& best = *bids_.rbegin();
            result.has_bid = true; result.bid_price = best.first; result.bid_qty = best.second;
        }
        if (!asks_.empty()) {
            const auto& best = *asks_.begin();
            result.has_ask = true; result.ask_price = best.first; result.ask_qty = best.second;
        }
        return result;
    }
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(std::min<std::size_t>(static_cast<std::size_t>(depth), bids_.size()));
        for (auto it = bids_.rbegin(); it != bids_.rend() && depth-- > 0; ++it)
            result.push_back({it->first, it->second});
        return result;
    }
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(std::min<std::size_t>(static_cast<std::size_t>(depth), asks_.size()));
        for (auto it = asks_.begin(); it != asks_.end() && depth-- > 0; ++it)
            result.push_back({it->first, it->second});
        return result;
    }
};
} // namespace ob
