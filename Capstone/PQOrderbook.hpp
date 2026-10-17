#pragma once
#include "types.hpp"
#include <vector>
#include <queue>
#include <map>
#include <unordered_map>
#include <cmath>

class OrderBookPQ{
    struct PQOrder {
        uint64_t id;
        Side side;
        uint32_t price;
        long qty;
    };
    struct BidComparator {
        bool operator()(const PQOrder& a, const PQOrder& b) const {
            if (a.price != b.price) {
                return a.price < b.price;
            }
            return a.id > b.id; 
        }
    };
    struct AskComparator {
        bool operator()(const PQOrder& a, const PQOrder& b) const {
            if (a.price != b.price) {
                return a.price > b.price;
            }
            return a.id > b.id;
        }
    };

private:
    mutable std::priority_queue<PQOrder, std::vector<PQOrder>, BidComparator> bids;
    mutable std::priority_queue<PQOrder, std::vector<PQOrder>, AskComparator> asks;
    std::unordered_map<uint32_t, long> bids_map; //price, qty
    std::unordered_map<uint32_t, long> asks_map; //price, qty
    std::unordered_map<uint64_t, PQOrder> orders; //id, order

public:
    OrderBookPQ() = default;
    
    void add_order(uint64_t id, Side side, double price, long qty){
        uint32_t p = std::llround(price*100.0);
        PQOrder o{id, side, p, qty};
        if(side == Side::Buy){
            bids_map[p] += qty;
            bids.push(o);
        } else {
            asks_map[p] += qty;
            asks.push(o);
        }
        orders[id] = o;     
    }

    bool cancel_order(uint64_t id) {
        auto order_it = orders.find(id);
        if(order_it == orders.end()){
            return false;
        }
        PQOrder o = order_it->second;
        if(o.side == Side::Buy){
            bids_map[o.price] -= o.qty;
        }else{
            asks_map[o.price] -= o.qty;
        }
        orders.erase(order_it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};

        while(!bids.empty() && orders.find(bids.top().id) == orders.end()){
            bids.pop();
        }
        if(!bids.empty()){
            uint32_t p = bids.top().price;
            bbo.has_bid = true;
            bbo.bid_price = p;
            bbo.bid_qty = bids_map.find(p)->second;
        }
        while(!asks.empty() && orders.find(asks.top().id) == orders.end()){
            asks.pop();
        }
        if(!asks.empty()){
            uint32_t p = asks.top().price;
            bbo.has_ask = true;
            bbo.ask_price = p;
            bbo.ask_qty = asks_map.find(p)->second;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> res;
        res.reserve(depth);
        std::vector<PQOrder> temp;
        uint32_t last_tick = 0;

        while(!bids.empty() && depth>0){
            PQOrder o = bids.top();
            bids.pop();
            if(orders.find(o.id) == orders.end()){
                continue;
            }
            if(res.empty() || o.price != last_tick){
                res.push_back({o.price / 100.0, bids_map.find(o.price)->second});
                last_tick = o.price;
                depth--;
            }
            temp.push_back(o);
        }
        for(auto &o : temp){
            bids.push(o);
        }

        return res;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> res;
        res.reserve(depth);
        std::vector<PQOrder> temp;
        uint32_t last_tick = 0;

        while(!asks.empty() && depth>0){
            PQOrder o = asks.top();
            asks.pop();
            if(orders.find(o.id) == orders.end()){
                continue;
            }
            if(res.empty() || o.price != last_tick){
                res.push_back({o.price / 100.0, asks_map.find(o.price)->second});
                last_tick = o.price;
                depth--;
            }
            temp.push_back(o);
        }
        for(auto &o : temp){
            asks.push(o);   
        }
        return res;
    }
};