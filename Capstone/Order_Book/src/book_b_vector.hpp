// Implementation B: sorted vectors + a tree for the orders.
//
//   price levels : one sorted std::vector<{key, qty}> per side (contiguous memory)
//   orders       : std::map<id, Order> (red-black tree ordered by id)
//   best price   : the LAST element of the vector, so we never search for it
//   cancel       : tree lookup of the id, then binary search for the price level
//
// To use the same code for both sides we sort on a "key": for bids the key is
// the tick, for asks it is -tick. The best price then always has the biggest
// key and sits at the back of the vector.
#pragma once

#include <algorithm>
#include <map>

#include "types.hpp"

class VectorBook {
public:
    bool add_order(uint64_t id, Side side, int tick, long qty) {
        if (qty <= 0 || !valid_tick(side, tick)) return false;
        if (!orders_.emplace(id, Order{qty, tick, side}).second) return false;

        add_qty(levels(side), key(side, tick), qty);
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        remove_qty(levels(o.side), key(o.side, o.tick), o.qty);

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = bids_.back().key;
            b.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = -asks_.back().key;
            b.ask_qty = asks_.back().qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth, 1); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth, -1); }

    size_t live_orders() const { return orders_.size(); }

private:
    struct Slot {
        int key;
        long qty;
    };
    using Levels = std::vector<Slot>;

    static int key(Side side, int tick) { return side == Side::Buy ? tick : -tick; }

    Levels& levels(Side side) { return side == Side::Buy ? bids_ : asks_; }

    static Levels::iterator find_pos(Levels& v, int k) {
        return std::lower_bound(v.begin(), v.end(), k,
                                [](const Slot& s, int value) { return s.key < value; });
    }

    static void add_qty(Levels& v, int k, long qty) {
        auto it = find_pos(v, k);
        if (it != v.end() && it->key == k) it->qty += qty;
        else v.insert(it, Slot{k, qty});  // shifts everything after it
    }

    static void remove_qty(Levels& v, int k, long qty) {
        auto it = find_pos(v, k);
        it->qty -= qty;
        if (it->qty == 0) v.erase(it);
    }

    // walk from the back (best price) towards the front
    static std::vector<Level> top(const Levels& v, int depth, int sign) {
        std::vector<Level> out;
        out.reserve(std::max(0, std::min(depth, 32)));
        for (auto it = v.rbegin(); it != v.rend() && (int)out.size() < depth; ++it)
            out.push_back({sign * it->key, it->qty});
        return out;
    }

    Levels bids_;
    Levels asks_;
    std::map<uint64_t, Order> orders_;
};
