// Implementation A: ordered tree (std::map) per side + std::unordered_map for
// order lookup.
//
//  * Orders       : std::unordered_map<id, {tick, qty, side}>  (node-based hash)
//  * Price levels : std::map<tick, qty>  (red-black tree), one per side.
//                   Bids ordered descending so begin() is the best bid,
//                   asks ordered ascending so begin() is the best ask.
//  * Best price   : *begin() of each map (tree-leftmost, O(1) amortised via
//                   the cached leftmost node, but still a pointer deref).
//  * Cancel       : hash lookup of id -> tick, then O(log L) tree lookup to
//                   decrement the level, erase the level if it hits zero.
//
//  add_order   : O(1) avg hash insert + O(log L) tree insert/find
//  cancel_order: O(1) avg hash find   + O(log L) tree find (+erase)
//  get_bbo     : O(1)
//  get_bids/asks(d): O(d)
#pragma once

#include <functional>
#include <map>
#include <unordered_map>

#include "order_book_common.hpp"

class OrderBookA {
public:
    OrderBookA() { orders_.reserve(1 << 20); }

    // Returns false (and changes nothing) for an out-of-band, off-tick or
    // non-finite price, qty <= 0, or an id that is already live.
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        return price_to_tick(price, tick) && add_order_ticks(id, side, tick, qty);
    }

    bool add_order_ticks(uint64_t id, Side side, int tick, long qty) {
        if (!valid_order(side, tick, qty)) return false;
        auto ins = orders_.emplace(id, OrderRec{tick, qty, side});
        if (!ins.second) return false;  // duplicate id
        if (side == Side::Buy)
            bids_[tick] += qty;
        else
            asks_[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const OrderRec rec = it->second;
        orders_.erase(it);
        if (rec.side == Side::Buy)
            erase_from(bids_, rec.tick, rec.qty);
        else
            erase_from(asks_, rec.tick, rec.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
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

private:
    struct OrderRec {
        int tick;
        long qty;
        Side side;
    };

    template <class Map>
    static void erase_from(Map& m, int tick, long qty) {
        auto it = m.find(tick);
        it->second -= qty;
        if (it->second == 0) m.erase(it);
    }

    template <class Map>
    static std::vector<Level> top(const Map& m, int depth) {
        std::vector<Level> out;
        out.reserve(depth_capacity(depth));
        for (auto it = m.begin(); it != m.end() && depth > 0; ++it, --depth)
            out.push_back(Level{to_price(it->first), it->second});
        return out;
    }

    std::map<int, long, std::greater<int>> bids_;  // best bid first
    std::map<int, long> asks_;                     // best ask first
    std::unordered_map<uint64_t, OrderRec> orders_;
};
