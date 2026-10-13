#pragma once

#include "order_book.hpp"

#include <array>

class ArrayOrderBook final : public OrderBook {
public:
    void add_order(OrderId id, Side side, Tick price, long quantity) override {
        validate_order(side, price, quantity);
        validate_new_id(used_ids_, id);
        auto& level = quantity_at(side, price);
        level += quantity;
        if (side == Side::Buy) best_bid_ = best_bid_ == -1 ? price : std::max(best_bid_, price);
        else best_ask_ = best_ask_ == -1 ? price : std::min(best_ask_, price);
        orders_.emplace(id, StoredOrder{side, price, quantity});
        used_ids_.insert(id);
    }

    bool cancel_order(OrderId id) override {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        auto& level = quantity_at(it->second.side, it->second.price);
        level -= it->second.quantity;
        if (level == 0) refresh_best(it->second.side, it->second.price);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO result;
        if (best_bid_ != -1) { result.has_bid = true; result.bid_price = best_bid_; result.bid_quantity = quantity_at(Side::Buy, best_bid_); }
        if (best_ask_ != -1) { result.has_ask = true; result.ask_price = best_ask_; result.ask_quantity = quantity_at(Side::Sell, best_ask_); }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        if (best_bid_ == -1) return result;
        for (Tick price = best_bid_; price >= 5000 && depth > 0; --price)
            if (quantity_at(Side::Buy, price) > 0) { result.push_back({price, quantity_at(Side::Buy, price)}); --depth; }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        if (best_ask_ == -1) return result;
        for (Tick price = best_ask_; price <= 15000 && depth > 0; ++price)
            if (quantity_at(Side::Sell, price) > 0) { result.push_back({price, quantity_at(Side::Sell, price)}); --depth; }
        return result;
    }

private:
    long& quantity_at(Side side, Tick price) {
        return side == Side::Buy ? bids_[price - 5000] : asks_[price - 10001];
    }
    long quantity_at(Side side, Tick price) const {
        return side == Side::Buy ? bids_[price - 5000] : asks_[price - 10001];
    }
    void refresh_best(Side side, Tick emptied) {
        if (side == Side::Buy && emptied == best_bid_) {
            best_bid_ = -1;
            for (Tick price = emptied - 1; price >= 5000; --price) if (quantity_at(side, price) > 0) { best_bid_ = price; break; }
        } else if (side == Side::Sell && emptied == best_ask_) {
            best_ask_ = -1;
            for (Tick price = emptied + 1; price <= 15000; ++price) if (quantity_at(side, price) > 0) { best_ask_ = price; break; }
        }
    }

    std::array<long, 5000> bids_{};
    std::array<long, 5000> asks_{};
    Tick best_bid_ = -1;
    Tick best_ask_ = -1;
    std::unordered_map<OrderId, StoredOrder> orders_;
    std::unordered_set<OrderId> used_ids_;
};
