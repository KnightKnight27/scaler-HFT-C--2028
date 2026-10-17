#pragma once
#include <functional>
#include <map>
#include "common.hpp"

// Implementation A: red-black trees (std::map) for both levels and orders
class MapBook {
    std::map<int, long, std::greater<int>> bids;
    std::map<int, long> asks;
    std::map<uint64_t, Order> orders;

    template <class M>
    static void reduce(M& levels, const Order& o) {
        auto it = levels.find(o.tick);
        it->second -= o.qty;
        if (it->second == 0) levels.erase(it);
    }

    template <class M>
    static std::vector<Level> top(const M& levels, int depth) {
        std::vector<Level> out;
        for (auto it = levels.begin(); it != levels.end() && (int)out.size() < depth; ++it)
            out.push_back({to_price(it->first), it->second});
        return out;
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int t = to_tick(price);
        if (!valid(side, t) || qty <= 0) return;
        if (!orders.emplace(id, Order{t, qty, side}).second) return;
        if (side == Side::Buy) bids[t] += qty;
        else asks[t] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) return false;
        Order o = it->second;
        orders.erase(it);
        if (o.side == Side::Buy) reduce(bids, o);
        else reduce(asks, o);
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0, 0, false, 0, 0};
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids.begin()->first);
            b.bid_qty = bids.begin()->second;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks.begin()->first);
            b.ask_qty = asks.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks, depth); }
};
