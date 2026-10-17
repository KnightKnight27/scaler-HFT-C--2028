// Implementation A: node-based tree + chained hash table.
//   levels : std::map<tick, qty>  (bids sorted descending, asks ascending)
//   orders : std::unordered_map<id, Order>
//   best   : begin() of the map
#pragma once
#include <functional>
#include <map>
#include <unordered_map>
#include "types.hpp"

class OrderBookMap {
    struct Order { int tick; long qty; Side side; };

public:
    static const char* name() { return "A (std::map levels + std::unordered_map orders)"; }

    bool add_order(uint64_t id, Side side, int tick, long qty) {
        if (qty <= 0 || !valid_tick(side, tick)) return false;
        if (!orders_.emplace(id, Order{tick, qty, side}).second) return false;  // duplicate id
        if (side == Side::Buy) bids_[tick] += qty; else asks_[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order o = it->second;
        if (o.side == Side::Buy) reduce(bids_, o); else reduce(asks_, o);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.empty()) { b.has_bid = true; b.bid_price = bids_.begin()->first; b.bid_qty = bids_.begin()->second; }
        if (!asks_.empty()) { b.has_ask = true; b.ask_price = asks_.begin()->first; b.ask_qty = asks_.begin()->second; }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

private:
    template <class M>
    static void reduce(M& m, const Order& o) {
        auto it = m.find(o.tick);
        it->second -= o.qty;
        if (it->second == 0) m.erase(it);
    }
    template <class M>
    static std::vector<Level> top(const M& m, int depth) {
        std::vector<Level> out;
        for (auto it = m.begin(); it != m.end() && static_cast<int>(out.size()) < depth; ++it)
            out.push_back({it->first, it->second});
        return out;
    }

    std::unordered_map<uint64_t, Order> orders_;
    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
};
