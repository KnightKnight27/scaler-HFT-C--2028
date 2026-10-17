#pragma once
// Implementation A: the "textbook" book built from standard node containers.
//
//   price levels : std::map<tick, total qty> per side (red-black tree)
//                  bids ordered high->low, asks low->high, so begin() is the best
//   orders       : std::unordered_map<id, {side, tick, qty}>
//
// Every level and every order is its own heap node, so this is the
// pointer-chasing baseline the other two designs are compared against.

#include <functional>
#include <map>
#include <unordered_map>

#include "common.hpp"

class BookA {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ticks::from_price(price);
        if (qty <= 0 || !ticks::in_band(side, tick) || !ticks::on_grid(price, tick)) return;
        auto [it, inserted] = orders_.try_emplace(id, Order{side, tick, qty});
        if (!inserted) return;  // duplicate id: ignored
        if (side == Side::Buy) bids_[tick] += qty;
        else asks_[tick] += qty;
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
            b.bid_price = ticks::to_price(bids_.begin()->first);
            b.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = ticks::to_price(asks_.begin()->first);
            b.ask_qty = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    template <class Map>
    static void reduce(Map& levels, int tick, long qty) {
        auto it = levels.find(tick);
        it->second -= qty;
        if (it->second == 0) levels.erase(it);
    }

    template <class Map>
    static std::vector<Level> top(const Map& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (auto it = levels.begin(); it != levels.end() && (int)out.size() < depth; ++it)
            out.push_back({ticks::to_price(it->first), it->second});
        return out;
    }

    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
