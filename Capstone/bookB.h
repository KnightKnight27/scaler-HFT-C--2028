#pragma once
#include "common.h"

// Implementation B
// price levels -> sorted std::vector of (price, qty), so its one contiguous block of memory
//   bids are sorted low to high  -> best bid is the LAST element
//   asks are sorted high to low  -> best ask is the LAST element
//   (best at the back so inserting near the best price only moves a few elements)
// finding a level = binary search
// orders -> std::map<id, order> (tree), cancel looks the id up in O(log n)
// orders dont need a list per level here because we only need total qty per price

struct OrderB {
    Side side;
    int price;
    long qty;
};

struct LevelB {
    int price;
    long qty;
};

class OrderBookB : public OrderBookBase {
public:
    vector<LevelB> bidLv; // ascending price
    vector<LevelB> askLv; // descending price
    map<uint64_t, OrderB> orders;

    // binary search, gives first index that is not "before" price
    // asc = true for bids (going up), false for asks (going down)
    int findPos(const vector<LevelB> &v, int price, bool asc) const {
        int lo = 0, hi = v.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            bool before;
            if (asc) before = v[mid].price < price;
            else before = v[mid].price > price;
            if (before) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    void addToLevel(vector<LevelB> &v, int price, long qty, bool asc) {
        int pos = findPos(v, price, asc);
        if (pos < (int)v.size() && v[pos].price == price) {
            v[pos].qty += qty;
        } else {
            LevelB lv;
            lv.price = price;
            lv.qty = qty;
            v.insert(v.begin() + pos, lv); // shifts everything after pos
        }
    }

    void removeFromLevel(vector<LevelB> &v, int price, long qty, bool asc) {
        int pos = findPos(v, price, asc);
        // level has to exist because the order exists
        v[pos].qty -= qty;
        if (v[pos].qty == 0) v.erase(v.begin() + pos);
    }

    void add_order(uint64_t id, Side side, double price, long qty) override {
        int t = toTicks(price);
        if (qty <= 0) return;
        if (!priceOk(side, t)) return;
        if (orders.count(id)) return;

        orders[id] = {side, t, qty};
        if (side == Side::Buy) addToLevel(bidLv, t, qty, true);
        else addToLevel(askLv, t, qty, false);
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders.find(id);
        if (it == orders.end()) return false;

        OrderB o = it->second;
        if (o.side == Side::Buy) removeFromLevel(bidLv, o.price, o.qty, true);
        else removeFromLevel(askLv, o.price, o.qty, false);
        orders.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO b = emptyBBO();
        if (!bidLv.empty()) {
            b.has_bid = true;
            b.bid_price = toPrice(bidLv.back().price);
            b.bid_qty = bidLv.back().qty;
        }
        if (!askLv.empty()) {
            b.has_ask = true;
            b.ask_price = toPrice(askLv.back().price);
            b.ask_qty = askLv.back().qty;
        }
        return b;
    }

    vector<Level> get_bids(int depth = 10) const override {
        vector<Level> res;
        for (int i = (int)bidLv.size() - 1; i >= 0 && (int)res.size() < depth; i--) {
            res.push_back({toPrice(bidLv[i].price), bidLv[i].qty});
        }
        return res;
    }

    vector<Level> get_asks(int depth = 10) const override {
        vector<Level> res;
        for (int i = (int)askLv.size() - 1; i >= 0 && (int)res.size() < depth; i--) {
            res.push_back({toPrice(askLv[i].price), askLv[i].qty});
        }
        return res;
    }
};
