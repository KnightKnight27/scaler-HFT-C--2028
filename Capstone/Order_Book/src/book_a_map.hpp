// Implementation A: standard containers.
//
//   price levels : std::map<tick, total qty>   (red-black tree, one per side)
//   orders       : std::unordered_map<id, Order> (hash table with linked buckets)
//   best price   : first element of the map (bids use std::greater so best is begin())
//   cancel       : hash lookup of the id, then tree lookup of the price level
#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>

#include "types.hpp"

class MapBook {
public:
    // Returns false (and does nothing) for a bad price, bad qty or a repeated id.
    bool add_order(uint64_t id, Side side, int tick, long qty) {
        if (qty <= 0 || !valid_tick(side, tick)) return false;
        if (!orders_.emplace(id, Order{qty, tick, side}).second) return false;

        if (side == Side::Buy) bids_[tick] += qty;
        else asks_[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        if (o.side == Side::Buy) reduce(bids_, o.tick, o.qty);
        else reduce(asks_, o.tick, o.qty);

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = bids_.begin()->first;
            b.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = asks_.begin()->first;
            b.ask_qty = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

    size_t live_orders() const { return orders_.size(); }

private:
    // bids: highest price first, asks: lowest price first
    using BidMap = std::map<int, long, std::greater<int>>;
    using AskMap = std::map<int, long>;

    template <class M>
    static void reduce(M& levels, int tick, long qty) {
        auto it = levels.find(tick);
        it->second -= qty;
        if (it->second == 0) levels.erase(it);
    }

    template <class M>
    static std::vector<Level> top(const M& levels, int depth) {
        std::vector<Level> out;
        out.reserve(std::max(0, std::min(depth, 32)));
        for (auto it = levels.begin(); it != levels.end() && (int)out.size() < depth; ++it)
            out.push_back({it->first, it->second});
        return out;
    }

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
