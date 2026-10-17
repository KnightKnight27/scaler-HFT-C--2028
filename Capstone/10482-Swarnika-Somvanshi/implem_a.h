// implem_a.h
// Implementation A: std::map for price levels, std::unordered_map for orders.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "common.h"

class OrderBookA {
public:
    void add_order(uint64_t id, Side side, int tick, long qty) {
        orders_[id] = Order{side, tick, qty};
        if (side == Side::Buy)
            bids_[tick] += qty;
        else
            asks_[tick] += qty;
    }

    // Returns false if the order doesn't exist or was already cancelled
    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        if (o.side == Side::Buy)
            remove_from_level(bids_, o.tick, o.qty);
        else
            remove_from_level(asks_, o.tick, o.qty);

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.empty()) {
            b.has_bid  = true;
            b.bid_tick = bids_.begin()->first;
            b.bid_qty  = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask  = true;
            b.ask_tick = asks_.begin()->first;
            b.ask_qty  = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

private:
    struct Order {
        Side side;
        int  tick;
        long qty;
    };

    // Bids use std::greater so the highest price is first.
    // Asks use the default order so the lowest price is first.
    using BidMap = std::map<int, long, std::greater<int>>;
    using AskMap = std::map<int, long>;

        // Subtract qty from a level and erase the level when it hits 0
    template <typename Map>
    static void remove_from_level(Map& m, int tick, long qty) {
        auto it = m.find(tick);
        it->second -= qty;
        if (it->second == 0) m.erase(it);
    }

    // Walk from the best price and collect up to `depth` levels
    template <typename Map>
    static std::vector<Level> top_levels(const Map& m, int depth) {
        std::vector<Level> out;
        out.reserve(depth);
        for (auto it = m.begin(); it != m.end() && (int)out.size() < depth; ++it)
            out.push_back(Level{it->first, it->second});
        return out;
    }


    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
