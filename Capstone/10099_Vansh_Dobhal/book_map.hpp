#pragma once

// Implementation A
// price levels: std::map<tick, total qty>, one map per side
// orders: std::unordered_map<id, order>

#include <map>
#include <unordered_map>
#include <functional>
#include "types.hpp"

class MapBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        if (qty <= 0 || !valid_price(side, tick) || orders.count(id))
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
        if (o.side == Side::Buy)
            remove_qty(bids, o.tick, o.qty);
        else
            remove_qty(asks, o.tick, o.qty);
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

    std::vector<Level> get_bids(int depth = 10) const {
        return top_levels(bids, depth);
    }

    std::vector<Level> get_asks(int depth = 10) const {
        return top_levels(asks, depth);
    }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    // bids sorted high to low, asks low to high, so begin() is always the best
    std::map<int, long, std::greater<int>> bids;
    std::map<int, long> asks;
    std::unordered_map<uint64_t, Order> orders;

    template <typename M>
    static void remove_qty(M &side, int tick, long qty) {
        auto it = side.find(tick);
        it->second -= qty;
        if (it->second == 0)
            side.erase(it);
    }

    template <typename M>
    static std::vector<Level> top_levels(const M &side, int depth) {
        std::vector<Level> out;
        for (auto it = side.begin(); it != side.end() && (int)out.size() < depth; ++it)
            out.push_back({to_price(it->first), it->second});
        return out;
    }
};
