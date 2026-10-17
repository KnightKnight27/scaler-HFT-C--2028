#pragma once
#include<vector>
#include<cstdint>

#include "types.hpp"

class BookC {
private:
    static constexpr int MIN_BID = 5000, MAX_ASK = 15000, BASE = 5000;

    alignas(64) long levels[MAX_ASK - BASE + 1]{};

    int bestBid = MIN_BID - 1, bestAsk = MAX_ASK + 1;
    std::vector<Order> orders;
public:
    void add_order(uint64_t id, Side side, int price, long qty) {
        levels[price - BASE] += qty;

        bool buy = (side == Side::Buy);
        int bid = buy ? price : 0;
        int ask = buy ? MAX_ASK + 1 : price;

        bestBid = (bid > bestBid) ? bid : bestBid;
        bestAsk = (ask < bestAsk) ? ask : bestAsk;

        if(id >= orders.size()) orders.resize(id * 2);
        orders[id] = Order{qty, price, side};
    }

    bool cancel_order(uint64_t id) {
        if(id >= orders.size() || !orders[id].qty) return false;

        Order &o = orders[id];
        levels[o.price - BASE] -= o.qty;
        o.qty = 0;

        if(!levels[o.price - BASE]) {
            while(bestBid >= MIN_BID && !levels[bestBid - BASE]) bestBid--;
            while(bestAsk <= MAX_ASK && !levels[bestAsk - BASE]) bestAsk++;
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};

        if(bestBid >= MIN_BID) {
            bbo.has_bid = true;
            bbo.bid_price = bestBid;
            bbo.bid_qty = levels[bestBid - BASE];
        }

        if(bestAsk <= MAX_ASK) {
            bbo.has_ask = true;
            bbo.ask_price = bestAsk;
            bbo.ask_qty = levels[bestAsk - BASE];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> res(depth);
        int i = 0;
        for(int b = bestBid; b >= MIN_BID && i < depth; b--) {
            if(levels[b - BASE]) res[i++] = {b, levels[b - BASE]};
        }
        res.resize(i);
        return res;
    }
    
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> res(depth);
        int i = 0;
        for(int a = bestAsk; a <= MAX_ASK && i < depth; a++) {
            if(levels[a - BASE]) res[i++] = {a, levels[a - BASE]};
        }
        res.resize(i);
        return res;
    }

    BookC() {
        orders.resize(1000001);
    }
};