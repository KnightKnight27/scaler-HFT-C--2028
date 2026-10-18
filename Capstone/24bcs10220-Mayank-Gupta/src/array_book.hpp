#pragma once

#include "types.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

namespace ob {

// C: direct price indexing, cached best indices, and packed live orders.
class ArrayBook {
public:
    void add_order(OrderId id, Side side, Price price, Quantity qty) {
        validate_order(side, price, qty);
        if (slots_.find(id) != slots_.end()) {
            throw std::invalid_argument("duplicate live order ID");
        }
        const int index = price - (side == Side::Buy ? bid_min : ask_min);
        auto& levels = side == Side::Buy ? bids_ : asks_;
        check_quantity_sum(levels[index], qty);

        orders_.push_back({id, Order{side, price, qty}});
        try {
            slots_.emplace(id, orders_.size() - 1);
        } catch (...) {
            orders_.pop_back();
            throw;
        }
        levels[index] += qty;
        if (side == Side::Buy) {
            best_bid_ = std::max(best_bid_, index);
        } else {
            best_ask_ = std::min(best_ask_, index);
        }
    }

    bool cancel_order(OrderId id) {
        auto slot = slots_.find(id);
        if (slot == slots_.end()) {
            return false;
        }
        const std::size_t index = slot->second;
        const Order order = orders_[index].order;
        const int price_index = order.price - (order.side == Side::Buy ? bid_min : ask_min);
        auto& levels = order.side == Side::Buy ? bids_ : asks_;
        levels[price_index] -= order.qty;

        // Fill the hole with the last order, then repair that order's ID lookup.
        if (index != orders_.size() - 1) {
            orders_[index] = orders_.back();
            slots_.find(orders_[index].id)->second = index;
        }
        orders_.pop_back();
        slots_.erase(slot);

        if (order.side == Side::Buy && price_index == best_bid_) {
            while (best_bid_ >= 0 && bids_[best_bid_] == 0) --best_bid_;
        } else if (order.side == Side::Sell && price_index == best_ask_) {
            while (best_ask_ < levels_per_side && asks_[best_ask_] == 0) ++best_ask_;
        }
        return true;
    }

    BBO get_bbo() const {
        BBO result;
        if (best_bid_ >= 0) {
            result.has_bid = true;
            result.bid_price = bid_min + best_bid_;
            result.bid_qty = bids_[best_bid_];
        }
        if (best_ask_ < levels_per_side) {
            result.has_ask = true;
            result.ask_price = ask_min + best_ask_;
            result.ask_qty = asks_[best_ask_];
        }
        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        const auto count = static_cast<std::size_t>(std::min(depth, levels_per_side));
        result.reserve(count);
        for (int i = best_bid_; i >= 0 && result.size() < count; --i) {
            if (bids_[i] != 0) result.push_back({bid_min + i, bids_[i]});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        const auto count = static_cast<std::size_t>(std::min(depth, levels_per_side));
        result.reserve(count);
        for (int i = best_ask_; i < levels_per_side && result.size() < count; ++i) {
            if (asks_[i] != 0) result.push_back({ask_min + i, asks_[i]});
        }
        return result;
    }

private:
    struct StoredOrder {
        OrderId id;
        Order order;
    };

    std::array<Quantity, levels_per_side> bids_{};
    std::array<Quantity, levels_per_side> asks_{};
    int best_bid_ = -1;
    int best_ask_ = levels_per_side;
    std::vector<StoredOrder> orders_;
    std::unordered_map<OrderId, std::size_t> slots_;
};

} // namespace ob

