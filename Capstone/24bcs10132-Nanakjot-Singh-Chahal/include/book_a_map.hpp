// Implementation A: red-black trees (std::map) for price levels,
// std::unordered_map for order-id lookup.
//
//   levels : std::map<tick, total_qty>   (bids sorted high->low, asks low->high)
//   orders : std::unordered_map<id, OrderInfo>
//   best   : begin() of the relevant map
#pragma once

#include <functional>
#include <map>
#include <unordered_map>

#include "common.hpp"

class MapOrderBook {
public:
    static constexpr const char* kName = "A (std::map + unordered_map)";

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (qty <= 0 || !ticks::valid(side, price, tick)) return;
        if (!orders_.emplace(id, OrderInfo{tick, side, qty}).second) return;  // duplicate id
        if (side == Side::Buy)
            bids_[tick] += qty;
        else
            asks_[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const OrderInfo o = it->second;
        orders_.erase(it);
        if (o.side == Side::Buy)
            reduce(bids_, o.tick, o.qty);
        else
            reduce(asks_, o.tick, o.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0.0, 0, false, 0.0, 0};
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
    template <class M>
    static void reduce(M& m, int tick, long qty) {
        auto it = m.find(tick);
        it->second -= qty;
        if (it->second == 0) m.erase(it);
    }

    template <class M>
    static std::vector<Level> top(const M& m, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        for (auto it = m.begin(); it != m.end() && static_cast<int>(out.size()) < depth; ++it)
            out.push_back({ticks::to_price(it->first), it->second});
        return out;
    }

    std::map<int, long, std::greater<int>> bids_;
    std::map<int, long> asks_;
    std::unordered_map<uint64_t, OrderInfo> orders_;
};
