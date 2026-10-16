// Rohan Ranjan-10428-Capstone project
//
// Implementation A: std::map for price levels + std::unordered_map for orders
//
// orders      : unordered_map  id -> {side, tick, qty}        (hash table)
// price levels: two std::map   tick -> total qty              (red-black trees)
//               bids sorted high -> low, asks low -> high, so begin() is always the best
// best price  : bids.begin() / asks.begin()
// cancel      : hash lookup by id, then find the level in the map and subtract

#pragma once

#include <functional>
#include <map>
#include <unordered_map>

#include "orderbook.hpp"

class BookA {
    struct OrderInfo {
        Side side;
        int  tick;
        long qty;
    };

    std::map<int, long, std::greater<int>> bids_; // highest price first
    std::map<int, long>                    asks_; // lowest price first
    std::unordered_map<uint64_t, OrderInfo> orders_;

    template <typename Map>
    static void remove_qty(Map& levels, int tick, long qty) {
        auto it = levels.find(tick);
        it->second -= qty;
        if (it->second == 0) levels.erase(it); // last order at this price is gone
    }

    template <typename Map>
    static std::vector<Level> top_levels(const Map& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(std::min<size_t>(depth, levels.size()));
        for (auto it = levels.begin(); it != levels.end() && (int)out.size() < depth; ++it)
            out.push_back({to_price(it->first), it->second});
        return out;
    }

public:
    static const char* name() { return "A (std::map + unordered_map)"; }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!parse_order(side, price, qty, tick)) return;
        if (!orders_.try_emplace(id, OrderInfo{side, tick, qty}).second) return; // id already live

        if (side == Side::Buy) bids_[tick] += qty;
        else                   asks_[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false; // unknown or already cancelled
        OrderInfo o = it->second;
        orders_.erase(it);

        if (o.side == Side::Buy) remove_qty(bids_, o.tick, o.qty);
        else                     remove_qty(asks_, o.tick, o.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            b.has_bid   = true;
            b.bid_price = to_price(bids_.begin()->first);
            b.bid_qty   = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask   = true;
            b.ask_price = to_price(asks_.begin()->first);
            b.ask_qty   = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }
};
