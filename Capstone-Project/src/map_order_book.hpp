#pragma once

#include "order_book.hpp"

#include <map>

class MapOrderBook final : public OrderBook {
public:
    void add_order(OrderId id, Side side, Tick price, long quantity) override {
        validate_order(side, price, quantity);
        validate_new_id(used_ids_, id);
        auto& levels = side == Side::Buy ? bids_ : asks_;
        levels[price] += quantity;
        orders_.emplace(id, StoredOrder{side, price, quantity});
        used_ids_.insert(id);
    }

    bool cancel_order(OrderId id) override {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        auto& levels = it->second.side == Side::Buy ? bids_ : asks_;
        const auto level = levels.find(it->second.price);
        level->second -= it->second.quantity;
        if (level->second == 0) levels.erase(level);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO result;
        if (!bids_.empty()) {
            const auto& [price, quantity] = *bids_.rbegin();
            result.has_bid = true; result.bid_price = price; result.bid_quantity = quantity;
        }
        if (!asks_.empty()) {
            const auto& [price, quantity] = *asks_.begin();
            result.has_ask = true; result.ask_price = price; result.ask_quantity = quantity;
        }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        for (auto it = bids_.rbegin(); it != bids_.rend() && depth-- > 0; ++it)
            result.push_back({it->first, it->second});
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        for (auto it = asks_.begin(); it != asks_.end() && depth-- > 0; ++it)
            result.push_back({it->first, it->second});
        return result;
    }

private:
    std::map<Tick, long> bids_;
    std::map<Tick, long> asks_;
    std::unordered_map<OrderId, StoredOrder> orders_;
    std::unordered_set<OrderId> used_ids_;
};
