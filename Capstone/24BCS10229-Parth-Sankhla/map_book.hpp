#pragma once

#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "common.hpp"

// A: map of price -> orders at that price (in time order)
struct PriceLevel {
    long total = 0;
    std::vector<Order> orders;
};

class MapBook {
public:
    void add_order(uint64_t id, Side side, int price, long qty) {
        if (qty <= 0 || !valid_price(side, price) || where.count(id))
            return;

        Order o = {id, side, price, qty, next_ts++};
        where[id] = o;
        PriceLevel& lvl = (side == Side::Buy) ? bids[price] : asks[price];
        lvl.orders.push_back(o);
        lvl.total += qty;
    }

    bool cancel_order(uint64_t id) {
        std::unordered_map<uint64_t, Order>::iterator it = where.find(id);
        if (it == where.end())
            return false;

        Order o = it->second;
        where.erase(it);

        if (o.side == Side::Buy) {
            std::map<int, PriceLevel, std::greater<int>>::iterator lvl = bids.find(o.price);
            remove_from_level(lvl->second, id, o.qty);
            if (lvl->second.orders.empty())
                bids.erase(lvl);
        } else {
            std::map<int, PriceLevel>::iterator lvl = asks.find(o.price);
            remove_from_level(lvl->second, id, o.qty);
            if (lvl->second.orders.empty())
                asks.erase(lvl);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b = {false, 0, 0, false, 0, 0};
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = bids.begin()->first;
            b.bid_qty = bids.begin()->second.total;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = asks.begin()->first;
            b.ask_qty = asks.begin()->second.total;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (std::map<int, PriceLevel, std::greater<int>>::const_iterator it = bids.begin();
             it != bids.end() && (int)out.size() < depth; ++it)
            out.push_back({it->first, it->second.total});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (std::map<int, PriceLevel>::const_iterator it = asks.begin();
             it != asks.end() && (int)out.size() < depth; ++it)
            out.push_back({it->first, it->second.total});
        return out;
    }

private:
    void remove_from_level(PriceLevel& lvl, uint64_t id, long qty) {
        for (size_t i = 0; i < lvl.orders.size(); i++) {
            if (lvl.orders[i].id == id) {
                lvl.orders.erase(lvl.orders.begin() + i);
                break;
            }
        }
        lvl.total -= qty;
    }

    std::map<int, PriceLevel, std::greater<int>> bids;
    std::map<int, PriceLevel> asks;
    std::unordered_map<uint64_t, Order> where;
    uint64_t next_ts = 0;
};
