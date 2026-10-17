#pragma once
#include "common.h"

// Implementation C (my own design)
// price levels -> plain array indexed by the tick, qty[tick] = total qty at that price
//   there are only 10000 prices so this is small (~120KB per side)
// best bid / best ask are kept in 2 ints, so get_bbo is just reading 2 array slots
//   when the best level becomes empty we walk the array to the next non empty one
// orders -> vector indexed directly by the order id (ids in the bench are 1,2,3...)
//   if an id is really big it goes into an unordered_map instead so memory doesnt explode

#define NUM_TICKS 15001
#define BIG_ID 50000000

struct OrderC {
    bool alive;
    Side side;
    int price;
    long qty;
};

class OrderBookC : public OrderBookBase {
public:
    vector<long> bidQty;
    vector<long> askQty;
    int bestBid; // 0 means no bids
    int bestAsk; // 0 means no asks
    vector<OrderC> byId;
    unordered_map<uint64_t, OrderC> bigIds;

    OrderBookC() {
        bidQty.assign(NUM_TICKS, 0);
        askQty.assign(NUM_TICKS, 0);
        bestBid = 0;
        bestAsk = 0;
        byId.resize(1024);
    }

    // returns the slot for this id or NULL if it doesnt exist
    // create=true makes room for it
    OrderC *getSlot(uint64_t id, bool create) {
        if (id < BIG_ID) {
            if (id >= byId.size()) {
                if (!create) return NULL;
                size_t n = byId.size();
                while (n <= id) n *= 2;
                if (n > BIG_ID) n = BIG_ID;
                byId.resize(n); // new slots have alive = false
            }
            return &byId[id];
        }
        auto it = bigIds.find(id);
        if (it != bigIds.end()) return &it->second;
        if (!create) return NULL;
        return &bigIds[id];
    }

    void add_order(uint64_t id, Side side, double price, long qty) override {
        int t = toTicks(price);
        if (qty <= 0) return;
        if (!priceOk(side, t)) return;

        OrderC *o = getSlot(id, true);
        if (o->alive) return; // id already in the book
        o->alive = true;
        o->side = side;
        o->price = t;
        o->qty = qty;

        if (side == Side::Buy) {
            bidQty[t] += qty;
            if (bestBid == 0 || t > bestBid) bestBid = t;
        } else {
            askQty[t] += qty;
            if (bestAsk == 0 || t < bestAsk) bestAsk = t;
        }
    }

    bool cancel_order(uint64_t id) override {
        OrderC *o = getSlot(id, false);
        if (o == NULL || !o->alive) return false;

        int p = o->price;
        if (o->side == Side::Buy) {
            bidQty[p] -= o->qty;
            if (bidQty[p] == 0 && p == bestBid) {
                // best bid is gone, walk down to find the next one
                int x = p - 1;
                while (x >= MIN_BID && bidQty[x] == 0) x--;
                if (x < MIN_BID) bestBid = 0;
                else bestBid = x;
            }
        } else {
            askQty[p] -= o->qty;
            if (askQty[p] == 0 && p == bestAsk) {
                // walk up for asks
                int x = p + 1;
                while (x <= MAX_ASK && askQty[x] == 0) x++;
                if (x > MAX_ASK) bestAsk = 0;
                else bestAsk = x;
            }
        }

        o->alive = false;
        if (id >= BIG_ID) bigIds.erase(id); // o is dead after this, dont touch it
        return true;
    }

    BBO get_bbo() const override {
        BBO b = emptyBBO();
        if (bestBid != 0) {
            b.has_bid = true;
            b.bid_price = toPrice(bestBid);
            b.bid_qty = bidQty[bestBid];
        }
        if (bestAsk != 0) {
            b.has_ask = true;
            b.ask_price = toPrice(bestAsk);
            b.ask_qty = askQty[bestAsk];
        }
        return b;
    }

    vector<Level> get_bids(int depth = 10) const override {
        vector<Level> res;
        if (bestBid == 0) return res;
        for (int t = bestBid; t >= MIN_BID && (int)res.size() < depth; t--) {
            if (bidQty[t] > 0) res.push_back({toPrice(t), bidQty[t]});
        }
        return res;
    }

    vector<Level> get_asks(int depth = 10) const override {
        vector<Level> res;
        if (bestAsk == 0) return res;
        for (int t = bestAsk; t <= MAX_ASK && (int)res.size() < depth; t++) {
            if (askQty[t] > 0) res.push_back({toPrice(t), askQty[t]});
        }
        return res;
    }
};
