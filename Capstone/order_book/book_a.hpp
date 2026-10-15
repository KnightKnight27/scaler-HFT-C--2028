// book_a.hpp - Implementation A: the "textbook" standard-library design.
//
//   orders       : std::unordered_map<OrderId, Rec>   (hash table, one heap node per order)
//   price levels : std::map<Price, qty>               (red-black tree, one heap node per level)
//   best price   : map.begin()  (bids use std::greater so begin() is the highest bid)
//   cancel lookup: hash lookup of the id, then a tree search for the order's price level
//
//   add_order    O(log L)   L = live price levels on that side (<= 5000)
//   cancel_order O(log L)   (hash lookup is O(1) average)
//   get_bbo      O(1)
//   get_bids/asks O(depth)
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "common.hpp"

class BookA {
public:
    bool add_order(OrderId id, Side side, Price px, long qty) {
        if (qty <= 0 || !valid_price(side, px)) return false;
        if (!orders_.emplace(id, Rec{px, qty, side}).second) return false;  // duplicate id
        if (side == Side::Buy) bids_[px] += qty;
        else                   asks_[px] += qty;
        return true;
    }

    bool cancel_order(OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;  // unknown or already cancelled
        const Rec r = it->second;
        orders_.erase(it);
        if (r.side == Side::Buy) take(bids_, r.price, r.qty);
        else                     take(asks_, r.price, r.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.empty()) {
            b.has_bid   = true;
            b.bid_price = bids_.begin()->first;
            b.bid_qty   = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask   = true;
            b.ask_price = asks_.begin()->first;
            b.ask_qty   = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

private:
    struct Rec {
        Price price;
        long  qty;
        Side  side;
    };

    // Subtract qty from a level and drop the level once it is empty.
    template <class Map>
    static void take(Map& m, Price px, long qty) {
        auto it = m.find(px);
        it->second -= qty;
        if (it->second == 0) m.erase(it);
    }

    template <class Map>
    static std::vector<Level> top(const Map& m, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(std::min<std::size_t>(m.size(), static_cast<std::size_t>(depth)));
        for (auto it = m.begin(); it != m.end() && static_cast<int>(out.size()) < depth; ++it)
            out.push_back(Level{it->first, it->second});
        return out;
    }

    std::unordered_map<OrderId, Rec>          orders_;
    std::map<Price, long, std::greater<Price>> bids_;  // best (highest) first
    std::map<Price, long>                      asks_;  // best (lowest) first
};
