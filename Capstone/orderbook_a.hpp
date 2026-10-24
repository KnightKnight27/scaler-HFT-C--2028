#pragma once

#include "utils.hpp"
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

class OrderBookA {
private:
    using Tick = int32_t;

    std::map<Tick, long, std::greater<Tick>> bids;
    std::map<Tick, long> asks;
    std::unordered_map<uint64_t, StoredOrder> orders;

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        Tick tick = price_to_tick(price);
        if (side == Side::Buy) {
            bids[tick] += qty;
        } else {
            asks[tick] += qty;
        }
        orders[id] = {side, tick, qty};
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) {
            return false;
        }

        const auto& order = it->second;

        if (order.side == Side::Buy) {
            auto bid_it = bids.find(order.tick);
            if (bid_it != bids.end()) {
                bid_it->second -= order.qty;
                if (bid_it->second <= 0) {
                    bids.erase(bid_it);
                }
            }
        } else {
            auto ask_it = asks.find(order.tick);
            if (ask_it != asks.end()) {
                ask_it->second -= order.qty;
                if (ask_it->second <= 0) {
                    asks.erase(ask_it);
                }
            }
        }

        orders.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        bbo.has_bid = false;
        bbo.has_ask = false;

        if (!bids.empty()) {
            auto it = bids.begin();
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(it->first);
            bbo.bid_qty = it->second;
        }

        if (!asks.empty()) {
            auto it = asks.begin();
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(it->first);
            bbo.ask_qty = it->second;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        for (auto it = bids.begin(); it != bids.end() && depth > 0; ++it, --depth) {
            result.push_back({tick_to_price(it->first), it->second});
        }

        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        for (auto it = asks.begin(); it != asks.end() && depth > 0; ++it, --depth) {
            result.push_back({tick_to_price(it->first), it->second});
        }

        return result;
    }
};