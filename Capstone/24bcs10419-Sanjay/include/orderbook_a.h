#pragma once

#include "common.h"
#include <map>
#include <unordered_map>
#include <vector>

class OrderBookA {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = Order{id, side, tick, qty};

        if (side == Side::Buy) {
            bids_[tick] += qty;
        } else {
            asks_[tick] += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }

        const Order& order = it->second;
        if (order.side == Side::Buy) {
            auto pit = bids_.find(order.tick);
            pit->second -= order.qty;
            if (pit->second <= 0) {
                bids_.erase(pit);
            }
        } else {
            auto pit = asks_.find(order.tick);
            pit->second -= order.qty;
            if (pit->second <= 0) {
                asks_.erase(pit);
            }
        }

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(bids_.begin()->first);
            bbo.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(asks_.begin()->first);
            bbo.ask_qty = asks_.begin()->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        result.reserve(depth);
        int count = 0;
        for (auto it = bids_.begin(); it != bids_.end() && count < depth; ++it, ++count) {
            result.push_back({tick_to_price(it->first), it->second});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        result.reserve(depth);
        int count = 0;
        for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
            result.push_back({tick_to_price(it->first), it->second});
        }
        return result;
    }

private:
    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
