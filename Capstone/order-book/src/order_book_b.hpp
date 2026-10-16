// Implementation B: sorted std::vector (levels) + std::map (orders)
#pragma once
#include <cstdint>
#include <algorithm>
#include <map>
#include "common.hpp"

// Levels: sorted std::vector<{tick, qty}> with the BEST level at the back
//         (bids ascending, asks descending). Binary search finds a level.
// Orders: std::map<id, Order>  (balanced tree keyed by id).
// Best price = back() of the vector.   Cancel = tree lookup by id, then binary search by price.
struct LV { int tick; long qty; };

class OrderBookB {
    std::vector<LV> bids_;      
    std::vector<LV> asks_;     
    std::map<uint64_t, Order> orders_;

    static std::vector<LV>::iterator find_pos(std::vector<LV>& v, int tick, bool is_bid) {
        if (is_bid) return std::lower_bound(v.begin(), v.end(), tick, [](const LV& l, int t) { return l.tick < t; });
        return std::lower_bound(v.begin(), v.end(), tick, [](const LV& l, int t) { return l.tick > t; });
    }
    static std::vector<Level> top(const std::vector<LV>& v, int depth) {
        std::vector<Level> out;
        for (int i = (int)v.size() - 1; i >= 0 && (int)out.size() < depth; --i)
            out.push_back({to_price(v[i].tick), v[i].qty});
        return out;
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int t = to_tick(price);
        if (!valid(side, t, qty)) return;
        if (!orders_.emplace(id, Order{qty, t, side}).second) return;
        bool is_bid = (side == Side::Buy);
        std::vector<LV>& v = is_bid ? bids_ : asks_;
        auto it = find_pos(v, t, is_bid);
        if (it != v.end() && it->tick == t) it->qty += qty;   // level exists
        else v.insert(it, LV{t, qty});                        // new level (shifts elements)
    }
    bool cancel_order(uint64_t id) {
        auto oit = orders_.find(id);
        if (oit == orders_.end()) return false;
        Order o = oit->second;
        orders_.erase(oit);
        bool is_bid = (o.side == Side::Buy);
        std::vector<LV>& v = is_bid ? bids_ : asks_;
        auto it = find_pos(v, o.tick, is_bid);
        it->qty -= o.qty;
        if (it->qty == 0) v.erase(it);
        return true;
    }
    BBO get_bbo() const {
        BBO b = empty_bbo();
        if (!bids_.empty()) { b.has_bid = true; b.bid_price = to_price(bids_.back().tick); b.bid_qty = bids_.back().qty; }
        if (!asks_.empty()) { b.has_ask = true; b.ask_price = to_price(asks_.back().tick); b.ask_qty = asks_.back().qty; }
        return b;
    }
    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }
};