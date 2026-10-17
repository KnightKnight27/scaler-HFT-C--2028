#pragma once
#include<unordered_map>
#include<vector>
#include<cstdint>
#include<queue>
#include<functional>

#include "types.hpp"

class BookB {
private:
    std::priority_queue<int, std::vector<int>, std::greater<int>> asks;
    std::priority_queue<int> bids;
    std::unordered_map<uint64_t, Order> orders;
    std::unordered_map<int, long> qts; //qty of each price (asks and bids are ME)
public:
    void add_order(uint64_t id, Side side, int price, long qty) {
        auto qt = qts.find(price);
        if(qt != qts.end()) qt->second += qty;
        else {
            qts[price] = qty;
            if(side == Side::Buy) {
                bids.push(price);
            } else {
                asks.push(price);
            }
        }
        orders.emplace(id, Order{qty, price, side});
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if(it == orders.end()) return false;

        int price = it->second.price;
        long qty = it->second.qty;
        
        // a price with qty 0 is marked for deletion and erased when popped
        auto qt = qts.find(price);
        qt->second -= qty;

        orders.erase(it);

        return true;
    }

    BBO get_bbo() {
        BBO bbo{};

        if(bids.size()) {
            auto bid = bids.top();
            auto qt = qts.find(bid);
            while(bids.size() && !qt->second) {
                qts.erase(qt);
                bids.pop();
                if(bids.size()) {
                    bid = bids.top();
                    qt = qts.find(bid);
                }
            }
            if(bids.size()) {
                bbo.has_bid = true;
                bbo.bid_price = bid;
                bbo.bid_qty = qt->second;
            }
        }

        if(asks.size()) {
            auto ask = asks.top();
            auto qt = qts.find(ask);
            while(asks.size() && !qt->second) {
                qts.erase(qt);
                asks.pop();
                if(asks.size()) {
                    ask = asks.top();
                    qt = qts.find(ask);
                }
            }

            if(asks.size()) {
                bbo.has_ask = true;
                bbo.ask_price = ask;
                bbo.ask_qty = qt->second;
            }
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) {
        std::vector<Level> res(depth);

        int i = 0;
        while(bids.size() && i < depth) {
            auto bid = bids.top();
            auto qty = qts.find(bid);
            while(bids.size() && !qty->second) {
                qts.erase(qty);
                bids.pop();
                if(bids.size()) {
                    bid = bids.top();
                    qty = qts.find(bid);
                }
            }

            if(!bids.size()) continue;
            res[i++] = {bid, qty->second};
            bids.pop();
        }
        res.resize(i);

        for(const auto &x : res) bids.push(x.price);
        return res;
    }
    
    std::vector<Level> get_asks(int depth = 10) {
        std::vector<Level> res(depth);
        
        int i = 0;
        while(asks.size() && i < depth) {
            auto ask = asks.top();
            auto qty = qts.find(ask);
            while(asks.size() && !qty->second) {
                qts.erase(qty);
                asks.pop();
                if(asks.size()) {
                    ask = asks.top();
                    qty = qts.find(ask);
                }
            }

            if(!asks.size()) continue;
            res[i++] = {ask, qty->second};
            asks.pop();
        }
        res.resize(i);

        for(const auto &x : res) asks.push(x.price);
        return res;
    }

    BookB() {
        orders.reserve(1000000);
        qts.reserve(10000);
    }
};