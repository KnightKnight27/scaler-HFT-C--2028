#pragma once

#include <functional>
#include <map>
#include <unordered_map>

#include "common.hpp"

class MapBook {
public:
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick = checked_tick(side, price);
        if (tick < 0 || qty <= 0) return false;
        if (!orders_.emplace(id, Order{side, tick, qty}).second) return false;

        if (side == Side::Buy) bids_[tick] += qty;
        else asks_[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        Order o = it->second;
        orders_.erase(it);
        if (o.side == Side::Buy) reduce(bids_, o.tick, o.qty);
        else reduce(asks_, o.tick, o.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0.0, 0, false, 0.0, 0};
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
        for (auto it = levels.begin(); it != levels.end() && (int)out.size() < depth; ++it)
            out.push_back({to_price(it->first), it->second});
        return out;
    }

    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
