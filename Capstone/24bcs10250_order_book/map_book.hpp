#pragma once

#include <functional>
#include <map>

#include "common.hpp"

// Implementation A: std::map for everything
class MapBook {
public:
    void add_order(uint64_t id, Side side, Price price, long qty) {
        if (!valid_order(side, price, qty)) return;
        if (orders.count(id)) return;

        orders[id] = {side, price, qty};
        if (side == Side::Buy) bids[price] += qty;
        else asks[price] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) return false;

        Order o = it->second;
        orders.erase(it);
        if (o.side == Side::Buy) remove_qty(bids, o.price, o.qty);
        else remove_qty(asks, o.price, o.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = bids.begin()->first;
            b.bid_qty = bids.begin()->second;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = asks.begin()->first;
            b.ask_qty = asks.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks, depth); }

private:
    std::map<uint64_t, Order> orders;
    std::map<Price, long, std::greater<Price>> bids;  // highest price first
    std::map<Price, long> asks;                       // lowest price first

    template <class M>
    static void remove_qty(M& side, Price price, long qty) {
        auto it = side.find(price);
        it->second -= qty;
        if (it->second == 0) side.erase(it);
    }

    template <class M>
    static std::vector<Level> top_levels(const M& side, int depth) {
        std::vector<Level> out;
        for (auto it = side.begin(); it != side.end() && (int)out.size() < depth; ++it) {
            out.push_back({it->first, it->second});
        }
        return out;
    }
};
