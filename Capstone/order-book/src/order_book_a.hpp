// Implementation A: std::map (levels) + std::unordered_map (orders)
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include "common.hpp"

// Levels: std::map<tick, total_qty>   (bids sorted high->low, asks low->high)
// Orders: std::unordered_map<id, Order>
// Best price = first element of the map.   Cancel = hash lookup by id, then map lookup by price.
class OrderBookA {
    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, Order> orders_;

    template <class M> static void remove_qty(M& m, const Order& o) {
        auto it = m.find(o.tick);
        it->second -= o.qty;
        if (it->second == 0) m.erase(it);              // every order has qty>=1, so 0 => level empty
    }
    template <class M> static std::vector<Level> top(const M& m, int depth) {
        std::vector<Level> out;
        for (auto& kv : m) {
            if ((int)out.size() >= depth) break;
            out.push_back({to_price(kv.first), kv.second});
        }
        return out;
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int t = to_tick(price);
        if (!valid(side, t, qty)) return;
        if (!orders_.emplace(id, Order{qty, t, side}).second) return;   // duplicate id
        if (side == Side::Buy) bids_[t] += qty; else asks_[t] += qty;
    }
    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        Order o = it->second;
        orders_.erase(it);
        if (o.side == Side::Buy) remove_qty(bids_, o); else remove_qty(asks_, o);
        return true;
    }
    BBO get_bbo() const {
        BBO b = empty_bbo();
        if (!bids_.empty()) { b.has_bid = true; b.bid_price = to_price(bids_.begin()->first); b.bid_qty = bids_.begin()->second; }
        if (!asks_.empty()) { b.has_ask = true; b.ask_price = to_price(asks_.begin()->first); b.ask_qty = asks_.begin()->second; }
        return b;
    }
    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }
};