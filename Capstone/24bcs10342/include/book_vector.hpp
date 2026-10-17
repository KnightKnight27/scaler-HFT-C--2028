// Implementation B: contiguous sorted vectors + binary search.
//   levels : std::vector<Level> sorted ascending by "key", best level at the BACK
//            (key = tick for bids, -tick for asks, so one code path serves both sides)
//   orders : std::vector<Order> sorted by id (append is O(1) when ids increase),
//            located by binary search; cancelled orders are tombstoned (live=false)
//   best   : back() of the level vector
#pragma once
#include <algorithm>
#include "types.hpp"

class OrderBookVector {
    struct Order { uint64_t id; int tick; long qty; Side side; bool live; };

public:
    static const char* name() { return "B (sorted std::vector levels + sorted-by-id order vector)"; }

    OrderBookVector() { orders_.reserve(1 << 20); }

    bool add_order(uint64_t id, Side side, int tick, long qty) {
        if (qty <= 0 || !valid_tick(side, tick)) return false;
        if (orders_.empty() || id > orders_.back().id) {
            orders_.push_back({id, tick, qty, side, true});
        } else {  // out-of-order id: O(N) insert (never happens in the benchmark)
            auto it = find_order(id);
            if (it != orders_.end() && it->id == id) return false;  // duplicate id
            orders_.insert(it, {id, tick, qty, side, true});
        }
        add_level(side == Side::Buy ? bids_ : asks_, key(side, tick), qty);
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = find_order(id);
        if (it == orders_.end() || it->id != id || !it->live) return false;
        it->live = false;
        remove_level(it->side == Side::Buy ? bids_ : asks_, key(it->side, it->tick), it->qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.empty()) { b.has_bid = true; b.bid_price = bids_.back().price; b.bid_qty = bids_.back().qty; }
        if (!asks_.empty()) { b.has_ask = true; b.ask_price = -asks_.back().price; b.ask_qty = asks_.back().qty; }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth, +1); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth, -1); }

private:
    static int key(Side s, int tick) { return s == Side::Buy ? tick : -tick; }

    std::vector<Order>::iterator find_order(uint64_t id) {
        return std::lower_bound(orders_.begin(), orders_.end(), id,
                                [](const Order& o, uint64_t v) { return o.id < v; });
    }
    static std::vector<Level>::iterator find_level(std::vector<Level>& v, int k) {
        return std::lower_bound(v.begin(), v.end(), k, [](const Level& l, int x) { return l.price < x; });
    }
    static void add_level(std::vector<Level>& v, int k, long qty) {
        auto it = find_level(v, k);
        if (it != v.end() && it->price == k) it->qty += qty;
        else v.insert(it, Level{k, qty});
    }
    static void remove_level(std::vector<Level>& v, int k, long qty) {
        auto it = find_level(v, k);
        it->qty -= qty;
        if (it->qty == 0) v.erase(it);
    }
    static std::vector<Level> top(const std::vector<Level>& v, int depth, int sign) {
        std::vector<Level> out;
        size_t n = depth <= 0 ? 0 : std::min(static_cast<size_t>(depth), v.size());
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const Level& l = v[v.size() - 1 - i];
            out.push_back({sign * l.price, l.qty});
        }
        return out;
    }

    std::vector<Order> orders_;
    std::vector<Level> bids_;  // key = tick
    std::vector<Level> asks_;  // key = -tick
};
