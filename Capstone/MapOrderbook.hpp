#pragma once
#include <map>
#include <unordered_map>
#include <vector>
#include <cmath>
#include "types.hpp"

class OrderBookMap {
private:
    std::unordered_map<uint64_t, Order> orders; // id, order
    std::map<uint32_t, long, std::greater<>> bids; // price, qty
    std::map<uint32_t, long> asks; // price, qty
    
public:
    OrderBookMap() = default;
    void add_order(uint64_t id, Side side, double price, long qty){
        std::uint32_t p = std::llround(price*100.0);
        if(side == Side::Buy){
            bids[p] += qty;
        }else{
            asks[p] += qty;
        }
        orders[id] = {id, side, p, qty};
    }

    bool cancel_order(uint64_t id){
        auto order_it = orders.find(id);
        if(order_it == orders.end()){
            return false;
        }
        Order o = order_it->second;
        if(o.side == Side::Buy){
            auto it = bids.find(o.price);
            if(it == bids.end()){
                return false;
            }
            it->second -= o.qty;
            if(it->second == 0){
                bids.erase(it);
            }
        }else{
            auto it = asks.find(o.price);
            if(it == asks.end()){
                return false;
            }
            it->second -= o.qty;
            if(it->second == 0){
                asks.erase(it);
            }
        }
        orders.erase(order_it);
        return true;
    }

    BBO get_bbo() const{
        BBO bbo{};
        
        if (!bids.empty()) {
            auto bids_it = bids.begin();
            bbo.has_bid = true;
            bbo.bid_price = bids_it->first;
            bbo.bid_qty = bids_it->second;
        }
        
        if (!asks.empty()) {
            auto asks_it = asks.begin();
            bbo.has_ask = true;
            bbo.ask_price = asks_it->first;
            bbo.ask_qty = asks_it->second;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const{
        std::vector<Level> res;
        res.reserve(depth);
        auto it = bids.begin();
        while(it != bids.end() && depth-->0){
            res.push_back({it->first/100.0, it->second});
            it++;
        }
        return res;
    }   

    std::vector<Level> get_asks(int depth = 10) const{
        std::vector<Level> res;
        res.reserve(depth);
        auto it = asks.begin();
        while(it != asks.end() && depth-->0){
            res.push_back({it->first/100.0, it->second});
            it++;
        }
        return res;
    }
};
    