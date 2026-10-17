#pragma once

// Implementation A: the simple one, standard containers only.
//   price levels -> std::map<tick, total qty> per side (red-black tree)
//   orders       -> std::unordered_map<id, Order>
// Bids use std::greater so begin() is always the best price on both sides.

#include <functional>
#include <map>
#include <unordered_map>

#include "common.hpp"

class MapBook {
public:
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!valid_qty(qty) || !price_to_tick(side, price, tick)) return false;

        auto res = orders_.emplace(id, Order{side, tick, qty});
        if (!res.second) return false;  // id already used

        if (side == Side::Buy)
            bids_[tick] += qty;
        else
            asks_[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        if (o.side == Side::Buy)
            reduce(bids_, o.tick, o.qty);
        else
            reduce(asks_, o.tick, o.qty);

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids_.begin()->first);
            b.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks_.begin()->first);
            b.ask_qty = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

    size_t order_count() const { return orders_.size(); }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, Order> orders_;

    template <class LevelMap>
    static void reduce(LevelMap& levels, int tick, long qty) {
        auto lv = levels.find(tick);
        lv->second -= qty;
        if (lv->second == 0) levels.erase(lv);  // qty >= 1 so 0 means no orders left
    }

    template <class LevelMap>
    static std::vector<Level> top(const LevelMap& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth < 64 ? depth : 64);
        for (auto it = levels.begin(); it != levels.end() && (int)out.size() < depth; ++it)
            out.push_back({to_price(it->first), it->second});
        return out;
    }
};
