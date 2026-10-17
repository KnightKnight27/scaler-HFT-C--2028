#pragma once

#include <functional>
#include <map>

#include "common.hpp"

// Implementation A: everything lives in red-black trees (std::map).
//   levels: price -> total qty, one map per side, best price is begin()
//   orders: id -> Order
class MapBook {
public:
    void add_order(uint64_t id, Side side, Price price, long qty) {
        if (!valid_order(side, price, qty)) return;
        if (!orders_.emplace(id, Order{side, price, qty}).second) return;  // id already live
        if (side == Side::Buy) bids_[price] += qty;
        else asks_[price] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order& o = it->second;
        if (o.side == Side::Buy) reduce(bids_, o.price, o.qty);
        else reduce(asks_, o.price, o.qty);
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

private:
    template <class M>
    static void reduce(M& m, Price price, long qty) {
        auto it = m.find(price);
        it->second -= qty;
        if (it->second == 0) m.erase(it);
    }

    template <class M>
    static std::vector<Level> top(const M& m, int depth) {
        std::vector<Level> out;
        for (auto it = m.begin(); it != m.end() && (int)out.size() < depth; ++it)
            out.push_back({it->first, it->second});
        return out;
    }

    std::map<Price, long, std::greater<Price>> bids_;  // best (highest) first
    std::map<Price, long> asks_;                       // best (lowest) first
    std::map<uint64_t, Order> orders_;
};
