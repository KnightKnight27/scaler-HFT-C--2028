#pragma once
#include "types.hpp"
#include <vector>
#include <unordered_map>
#include <cmath>

class OrderBookArray {

private:
//          0       1       2         10000
//         [5000] [5001] [5002]      [15000]
// qty ->   ^      ^     ^               ^   

    long bids[MAX_BID - MIN_BID + 1]{};   
    long asks[MAX_ASK - MIN_ASK + 1]{};   
    int best_bid = -1;
    int best_ask = -1;
    std::unordered_map<uint64_t, Order> orders; //id, order

public:
    OrderBookArray() {}

    void add_order(uint64_t id, Side side, double price, long qty){
        uint32_t p = std::llround(price*100.0);
        Order o = {id, side, p, qty};
        if(side == Side::Buy){
            bids[p-MIN_BID] += qty;
            orders[id] = o;
            if(best_bid == -1 || p > best_bid){
                best_bid = p;
            }
        }
        else{
            asks[p-MIN_ASK] += qty;
            orders[id] = o;
            if(best_ask == -1 || p < best_ask){
                best_ask = p;
            }
        }
    }

    bool cancel_order(uint64_t id){
        auto it = orders.find(id);
        if(it == orders.end()){
            return false;
        }
        Order o = it->second;
        uint64_t p = o.price;
        if(o.side == Side::Buy){
            bids[p - MIN_BID] -= o.qty;
            if(best_bid == p && bids[best_bid - MIN_BID] == 0){
                while(best_bid >= MIN_BID && bids[best_bid - MIN_BID] == 0){
                    best_bid--;
                }
                if(best_bid < MIN_BID){
                    best_bid = -1;
                }
            }
        } else {
            asks[p - MIN_ASK] -= o.qty;
            if(best_ask == p && asks[best_ask - MIN_ASK] == 0){
                while(best_ask <= MAX_ASK && asks[best_ask - MIN_ASK] == 0){
                    best_ask++;
                }
                if(best_ask > MAX_ASK){
                    best_ask = -1;
                }
            }
        }
        orders.erase(it);
        return true;
    }

    BBO get_bbo() const{
        BBO bbo{};

        if(best_bid!=-1){
            bbo.has_bid = true;
            bbo.bid_price = best_bid;
            bbo.bid_qty = bids[best_bid-MIN_BID];
        }
        if(best_ask != -1){
            bbo.has_ask = true;
            bbo.ask_price = best_ask;
            bbo.ask_qty = asks[best_ask-MIN_ASK];
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const{
        std::vector<Level> res;
        if(best_bid == -1){
            return res;
        }
        res.reserve(depth);
        int temp = best_bid;
        while(temp >= MIN_BID && depth > 0){
            if(bids[temp - MIN_BID] > 0){
                res.push_back({temp/100.0, bids[temp-MIN_BID]});
                depth--;
            }
            temp--;
        }
        return res;
    }

    std::vector<Level> get_asks(int depth = 10) const{
        std::vector<Level> res;
        if(best_ask == -1){
            return res;
        }
        res.reserve(depth);
        int temp = best_ask;
        while(temp <= MAX_ASK && depth > 0){
            if(asks[temp - MIN_ASK] > 0){
                res.push_back({temp/100.0, asks[temp-MIN_ASK]});
                depth--;
            }
            temp++;
        }
        return res;
    }

};