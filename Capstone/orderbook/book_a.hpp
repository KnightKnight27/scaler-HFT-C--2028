// Implementation A: ordered map (std::map) + hash map for order lookup.
//
// How it works:
//   - bids/asks: std::map<tick, total qty>. std::map keeps keys sorted, so
//     the best bid is the LAST element (rbegin) and the best ask is the
//     FIRST element (begin).
//   - orders: unordered_map<id, OrderInfo> so cancel_order can find an
//     order by id in O(1) instead of searching the whole book.
//
// Complexity:
//   add_order    O(log M)  (map insert / update, M = live price levels)
//   cancel_order O(log M)  (map update, hash lookup is O(1))
//   get_bbo      O(1)      (just look at both ends of the maps)
//   get_bids/asks O(depth) (walk from the best end)

#pragma once

#include "common.hpp"

#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

class MapOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = {side, tick, qty};
        if (side == Side::Buy)
            bids_[tick] += qty;
        else
            asks_[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end())
            return false; // unknown or already cancelled

        OrderInfo o = it->second;
        orders_.erase(it);

        std::map<int, long> &levels = (o.side == Side::Buy) ? bids_ : asks_;
        auto lv = levels.find(o.tick);
        if (lv != levels.end()) {
            lv->second -= o.qty;
            if (lv->second <= 0)
                levels.erase(lv); // level empty, drop it
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            auto it = bids_.rbegin(); // highest bid
            b.has_bid = true;
            b.bid_price = tick_to_price(it->first);
            b.bid_qty = it->second;
        }
        if (!asks_.empty()) {
            auto it = asks_.begin(); // lowest ask
            b.has_ask = true;
            b.ask_price = tick_to_price(it->first);
            b.ask_qty = it->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = bids_.rbegin(); it != bids_.rend() && (int)out.size() < depth; ++it)
            out.push_back({tick_to_price(it->first), it->second});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = asks_.begin(); it != asks_.end() && (int)out.size() < depth; ++it)
            out.push_back({tick_to_price(it->first), it->second});
        return out;
    }

private:
    struct OrderInfo {
        Side side;
        int tick;
        long qty;
    };

    std::map<int, long> bids_; // tick -> total qty, sorted ascending
    std::map<int, long> asks_; // tick -> total qty, sorted ascending
    std::unordered_map<uint64_t, OrderInfo> orders_; // id -> order
};
