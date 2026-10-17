#pragma once
#include "common.h"

// Implementation A
// price levels -> std::map (red black tree), key = price in ticks
// every level has the total qty + a std::list with the orders at that price
// order id -> unordered_map that remembers side, price and the list iterator
// so cancel can jump straight to the order

struct OrderA {
    uint64_t id;
    long qty;
};

struct LevelA {
    long total = 0;
    list<OrderA> orders;
};

struct OrderInfoA {
    Side side;
    int price;
    list<OrderA>::iterator it;
};

class OrderBookA : public OrderBookBase {
public:
    map<int, LevelA, greater<int>> bids; // biggest price first
    map<int, LevelA> asks;               // smallest price first
    unordered_map<uint64_t, OrderInfoA> idMap;

    void add_order(uint64_t id, Side side, double price, long qty) override {
        int t = toTicks(price);
        if (qty <= 0) return;
        if (!priceOk(side, t)) return;
        if (idMap.count(id)) return; // id already in the book

        OrderInfoA info;
        info.side = side;
        info.price = t;

        if (side == Side::Buy) {
            LevelA &lv = bids[t]; // makes the level if its not there
            lv.total += qty;
            lv.orders.push_back({id, qty});
            info.it = prev(lv.orders.end());
        } else {
            LevelA &lv = asks[t];
            lv.total += qty;
            lv.orders.push_back({id, qty});
            info.it = prev(lv.orders.end());
        }
        idMap[id] = info;
    }

    bool cancel_order(uint64_t id) override {
        auto f = idMap.find(id);
        if (f == idMap.end()) return false; // never seen or already cancelled

        OrderInfoA info = f->second;
        if (info.side == Side::Buy) {
            auto lv = bids.find(info.price);
            lv->second.total -= info.it->qty;
            lv->second.orders.erase(info.it);
            if (lv->second.orders.empty()) bids.erase(lv);
        } else {
            auto lv = asks.find(info.price);
            lv->second.total -= info.it->qty;
            lv->second.orders.erase(info.it);
            if (lv->second.orders.empty()) asks.erase(lv);
        }
        idMap.erase(f);
        return true;
    }

    BBO get_bbo() const override {
        BBO b = emptyBBO();
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = toPrice(bids.begin()->first);
            b.bid_qty = bids.begin()->second.total;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = toPrice(asks.begin()->first);
            b.ask_qty = asks.begin()->second.total;
        }
        return b;
    }

    vector<Level> get_bids(int depth = 10) const override {
        vector<Level> res;
        for (auto it = bids.begin(); it != bids.end() && (int)res.size() < depth; it++) {
            res.push_back({toPrice(it->first), it->second.total});
        }
        return res;
    }

    vector<Level> get_asks(int depth = 10) const override {
        vector<Level> res;
        for (auto it = asks.begin(); it != asks.end() && (int)res.size() < depth; it++) {
            res.push_back({toPrice(it->first), it->second.total});
        }
        return res;
    }
};
