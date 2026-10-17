#pragma once

#include "types.hpp"
#include <algorithm>
#include <unordered_map>

namespace ob {
class VectorBook {
    std::vector<Order> orders_;
    std::vector<std::size_t> free_slots_;
    std::unordered_map<OrderId, std::size_t> index_;
    std::vector<Level> bids_, asks_;

    static auto locate(std::vector<Level>& levels, Price price, Side side) {
        return std::lower_bound(levels.begin(), levels.end(), price,
            [side](const Level& level, Price p) {
                return side == Side::Buy ? level.price > p : level.price < p;
            });
    }

    static std::vector<Level> depth(const std::vector<Level>& levels, int count) {
        if (count <= 0) return {};
        const auto n = std::min(levels.size(), static_cast<std::size_t>(count));
        return {levels.begin(), levels.begin() + n};
    }

public:
    explicit VectorBook(std::size_t capacity = 0) {
        bids_.reserve(price_count);
        asks_.reserve(price_count);
        orders_.reserve(capacity);
        free_slots_.reserve(capacity);
        index_.max_load_factor(0.7f);
        index_.reserve(capacity);
    }

    void add_order(OrderId id, Side side, Price price, Quantity qty) {
        validate(side, price, qty);
        if (index_.find(id) != index_.end())
            throw std::invalid_argument("duplicate live order ID");
        const std::size_t slot = free_slots_.empty() ? orders_.size() : free_slots_.back();
        if (free_slots_.empty()) orders_.push_back({side, price, qty});
        else {
            free_slots_.pop_back();
            orders_[slot] = {side, price, qty};
        }
        index_.emplace(id, slot);
        auto& levels = side == Side::Buy ? bids_ : asks_;
        auto it = locate(levels, price, side);
        if (it != levels.end() && it->price == price) it->qty += qty;
        else levels.insert(it, {price, qty});
    }

    bool cancel_order(OrderId id) {
        auto entry = index_.find(id);
        if (entry == index_.end()) return false;
        const Order order = orders_[entry->second];
        auto& levels = order.side == Side::Buy ? bids_ : asks_;
        auto it = locate(levels, order.price, order.side);
        it->qty -= order.qty;
        if (it->qty == 0) levels.erase(it);
        free_slots_.push_back(entry->second);
        index_.erase(entry);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids_.front().price;
            bbo.bid_qty = bids_.front().qty;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks_.front().price;
            bbo.ask_qty = asks_.front().qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int count = 10) const { return depth(bids_, count); }
    std::vector<Level> get_asks(int count = 10) const { return depth(asks_, count); }
};
}
