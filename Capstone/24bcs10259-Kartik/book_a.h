#pragma once
#include <map>
#include <unordered_map>
#include <functional>
#include "common.h"

// A: std::map per side (tick -> total qty) + unordered_map for orders

class OrderBookA {
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    std::map<int, long, std::greater<int>> bids;  // begin() = best bid
    std::map<int, long> asks;
    std::unordered_map<uint64_t, Order> orders;

public:
    OrderBookA() {
        orders.reserve(1 << 20);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        if (qty <= 0 || !valid_tick(side, tick))
            return;
        if (orders.count(id))
            return;

        orders[id] = {side, tick, qty};
        if (side == Side::Buy)
            bids[tick] += qty;
        else
            asks[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end())
            return false;

        Order o = it->second;
        orders.erase(it);

        if (o.side == Side::Buy) {
            auto lv = bids.find(o.tick);
            lv->second -= o.qty;
            if (lv->second == 0)
                bids.erase(lv);
        } else {
            auto lv = asks.find(o.tick);
            lv->second -= o.qty;
            if (lv->second == 0)
                asks.erase(lv);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = tick_to_price(bids.begin()->first);
            b.bid_qty = bids.begin()->second;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = tick_to_price(asks.begin()->first);
            b.ask_qty = asks.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> res;
        for (auto it = bids.begin(); it != bids.end() && (int)res.size() < depth; ++it)
            res.push_back({tick_to_price(it->first), it->second});
        return res;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> res;
        for (auto it = asks.begin(); it != asks.end() && (int)res.size() < depth; ++it)
            res.push_back({tick_to_price(it->first), it->second});
        return res;
    }
};
