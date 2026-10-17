#pragma once

// Implementation B
// price levels: sorted std::vector of (tick, qty) per side, best level kept at the back
// orders: std::map<id, order> (balanced tree instead of a hash table)

#include <map>
#include <algorithm>
#include "types.hpp"

class VectorBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        if (qty <= 0 || !valid_price(side, tick) || orders.count(id))
            return;

        orders[id] = {side, tick, qty};
        if (side == Side::Buy)
            add_qty(bids, tick, qty, true);
        else
            add_qty(asks, tick, qty, false);
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end())
            return false;

        Order o = it->second;
        orders.erase(it);
        if (o.side == Side::Buy)
            remove_qty(bids, o.tick, o.qty, true);
        else
            remove_qty(asks, o.tick, o.qty, false);
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0, 0, false, 0, 0};
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids.back().tick);
            b.bid_qty = bids.back().qty;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks.back().tick);
            b.ask_qty = asks.back().qty;
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

    struct PriceLevel {
        int tick;
        long qty;
    };

    // bids: ascending ticks, so the highest bid is at the back
    // asks: descending ticks, so the lowest ask is at the back
    // most orders land near the top of the book, so inserts/erases near the back move few elements
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    std::map<uint64_t, Order> orders;

    // finds the position where tick is (or should be inserted)
    static std::vector<PriceLevel>::iterator find_pos(std::vector<PriceLevel> &v, int tick, bool ascending) {
        if (ascending)
            return std::lower_bound(v.begin(), v.end(), tick,
                                    [](const PriceLevel &l, int t) { return l.tick < t; });
        return std::lower_bound(v.begin(), v.end(), tick,
                                [](const PriceLevel &l, int t) { return l.tick > t; });
    }

    static void add_qty(std::vector<PriceLevel> &v, int tick, long qty, bool ascending) {
        auto it = find_pos(v, tick, ascending);
        if (it != v.end() && it->tick == tick)
            it->qty += qty;
        else
            v.insert(it, {tick, qty});
    }

    static void remove_qty(std::vector<PriceLevel> &v, int tick, long qty, bool ascending) {
        auto it = find_pos(v, tick, ascending);
        it->qty -= qty;
        if (it->qty == 0)
            v.erase(it);
    }

    static std::vector<Level> top_levels(const std::vector<PriceLevel> &v, int depth) {
        std::vector<Level> out;
        for (int i = (int)v.size() - 1; i >= 0 && (int)out.size() < depth; i--)
            out.push_back({to_price(v[i].tick), v[i].qty});
        return out;
    }
};
