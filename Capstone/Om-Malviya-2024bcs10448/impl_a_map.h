// impl_a_map.h — Implementation A: ordered std::map (balanced BST).
//
//   Price levels : std::map keyed by tick. Bids use std::greater so begin() is
//                  the highest (best) bid; asks use the default less so begin()
//                  is the lowest (best) ask. Map value = total resting qty.
//   Orders       : std::unordered_map<id, {side, tick, qty}> for cancellation.
//   Best price   : map.begin()               -> O(1)
//   add_order    : map insert/update O(log L) + hash insert O(1)
//   cancel_order : hash find O(1) + map update/erase O(log L)
//
// A level is removed from the map once its total qty reaches 0 (every order
// qty >= 1, so total == 0 iff no orders rest there).

#ifndef CAPSTONE_IMPL_A_MAP_H
#define CAPSTONE_IMPL_A_MAP_H

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "common.h"

class OrderBookA {
public:
    OrderBookA() { orders_.reserve(1 << 20); }

    void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ticks::to_tick(price);
        orders_.emplace(id, OrderRec{side, tick, qty});
        if (side == Side::Buy)
            bids_[tick] += qty;
        else
            asks_[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;  // unknown or already cancelled
        const OrderRec& o = it->second;
        if (o.side == Side::Buy)
            erase_from(bids_, o.tick, o.qty);
        else
            erase_from(asks_, o.tick, o.qty);
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            auto it = bids_.begin();  // highest bid
            b.has_bid = true;
            b.bid_price = ticks::to_price(it->first);
            b.bid_qty = it->second;
        }
        if (!asks_.empty()) {
            auto it = asks_.begin();  // lowest ask
            b.has_ask = true;
            b.ask_price = ticks::to_price(it->first);
            b.ask_qty = it->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (auto it = bids_.begin(); it != bids_.end() && (int)out.size() < depth; ++it)
            out.push_back({ticks::to_price(it->first), it->second});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        for (auto it = asks_.begin(); it != asks_.end() && (int)out.size() < depth; ++it)
            out.push_back({ticks::to_price(it->first), it->second});
        return out;
    }

private:
    struct OrderRec {
        Side side;
        int tick;
        long qty;
    };

    template <typename Map>
    static void erase_from(Map& m, int tick, long qty) {
        auto it = m.find(tick);
        if (it == m.end()) return;
        it->second -= qty;
        if (it->second <= 0) m.erase(it);
    }

    std::map<int, long, std::greater<int>> bids_;  // highest first
    std::map<int, long> asks_;                     // lowest first
    std::unordered_map<uint64_t, OrderRec> orders_;
};

#endif  // CAPSTONE_IMPL_A_MAP_H
