#pragma once

#include "types.hpp"
#include <algorithm>
#include <functional>
#include <map>

namespace ob {
class MapBook {
    std::map<OrderId, Order> orders_;
    std::map<Price, Quantity, std::greater<Price>> bids_;
    std::map<Price, Quantity> asks_;

    template<class Levels>
    static void subtract(Levels& levels, const Order& order) {
        auto it = levels.find(order.price);
        it->second -= order.qty;
        if (it->second == 0) levels.erase(it);
    }

    template<class Levels>
    static std::vector<Level> depth(const Levels& levels, int count) {
        std::vector<Level> result;
        if (count <= 0) return result;
        result.reserve(std::min(levels.size(), static_cast<std::size_t>(count)));
        for (auto it = levels.begin(); it != levels.end() && count > 0; ++it, --count)
            result.push_back({it->first, it->second});
        return result;
    }

public:
    explicit MapBook(std::size_t = 0) {}

    void add_order(OrderId id, Side side, Price price, Quantity qty) {
        validate(side, price, qty);
        if (!orders_.emplace(id, Order{side, price, qty}).second)
            throw std::invalid_argument("duplicate live order ID");
        if (side == Side::Buy) bids_[price] += qty;
        else asks_[price] += qty;
    }

    bool cancel_order(OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        if (it->second.side == Side::Buy) subtract(bids_, it->second);
        else subtract(asks_, it->second);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids_.begin()->first;
            bbo.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks_.begin()->first;
            bbo.ask_qty = asks_.begin()->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int count = 10) const { return depth(bids_, count); }
    std::vector<Level> get_asks(int count = 10) const { return depth(asks_, count); }
};
}
