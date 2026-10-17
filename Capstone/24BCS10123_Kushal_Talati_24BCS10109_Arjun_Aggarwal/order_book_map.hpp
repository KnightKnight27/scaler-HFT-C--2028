#pragma once
// impl A: std::map per side for levels, unordered_map for orders
#include "order_book_api.hpp"
#include <map>
#include <unordered_map>
#include <functional>

struct OrderBookMap {
    std::map<int32_t, long, std::greater<int32_t>> bids;  // begin() = highest
    std::map<int32_t, long> asks;                         // begin() = lowest
    std::unordered_map<uint64_t, OrderRec> orders;

    bool add_order(uint64_t id, Side side, double price, long qty) {
        int32_t tick = to_tick(price);
        if (qty <= 0 || !tick_in_band(side, tick)) return false;
        auto ins = orders.emplace(id, OrderRec{tick, static_cast<int32_t>(qty), side, true});
        if (!ins.second) return false;  // id already in use
        if (side == Side::Buy) bids[tick] += qty;
        else asks[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) return false;
        const OrderRec& o = it->second;
        if (o.side == Side::Buy) sub_level(bids, o.tick, o.qty);
        else sub_level(asks, o.tick, o.qty);
        orders.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bids.begin()->first);
            b.bid_qty = bids.begin()->second;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(asks.begin()->first);
            b.ask_qty = asks.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks, depth); }

private:
    template <class Map>
    static void sub_level(Map& m, int32_t tick, long qty) {
        auto it = m.find(tick);
        it->second -= qty;
        if (it->second == 0) m.erase(it);  // lvl empty now, kick it out so begin() moves
    }

    template <class Map>
    static std::vector<Level> top_levels(const Map& m, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        size_t want = static_cast<size_t>(depth);
        out.reserve(want < m.size() ? want : m.size());
        for (auto it = m.begin(); it != m.end() && out.size() < want; ++it)
            out.push_back(Level{to_price(it->first), it->second});
        return out;
    }
};
