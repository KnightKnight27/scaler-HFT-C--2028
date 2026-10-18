#pragma once

#include <algorithm>
#include <map>

#include "common.hpp"

class VectorBook {
public:
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick = checked_tick(side, price);
        if (tick < 0 || qty <= 0) return false;
        if (!orders_.emplace(id, Order{side, tick, qty}).second) return false;

        std::vector<Tick>& levels = (side == Side::Buy) ? bids_ : asks_;
        auto it = find(levels, side, tick);
        if (it != levels.end() && it->tick == tick) it->qty += qty;
        else levels.insert(it, Tick{tick, qty});
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto oit = orders_.find(id);
        if (oit == orders_.end()) return false;

        Order o = oit->second;
        orders_.erase(oit);

        std::vector<Tick>& levels = (o.side == Side::Buy) ? bids_ : asks_;
        auto it = find(levels, o.side, o.tick);
        it->qty -= o.qty;
        if (it->qty == 0) levels.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0.0, 0, false, 0.0, 0};
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids_.back().tick);
            b.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks_.back().tick);
            b.ask_qty = asks_.back().qty;
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

    struct Tick {
        int tick;
        long qty;
    };

    // Bids ascending, asks descending: the best price is always at the back.
    static std::vector<Tick>::iterator find(std::vector<Tick>& levels, Side side, int tick) {
        if (side == Side::Buy)
            return std::lower_bound(levels.begin(), levels.end(), tick,
                                    [](const Tick& l, int t) { return l.tick < t; });
        return std::lower_bound(levels.begin(), levels.end(), tick,
                                [](const Tick& l, int t) { return l.tick > t; });
    }

    static std::vector<Level> top(const std::vector<Tick>& levels, int depth) {
        std::vector<Level> out;
        for (auto it = levels.rbegin(); it != levels.rend() && (int)out.size() < depth; ++it)
            out.push_back({to_price(it->tick), it->qty});
        return out;
    }

    std::vector<Tick> bids_;
    std::vector<Tick> asks_;
    std::map<uint64_t, Order> orders_;
};
