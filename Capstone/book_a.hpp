#pragma once
#include<map>
#include<unordered_map>
#include<vector>
#include<cstdint>

#include "types.hpp"

class BookA {
private:
    std::map<int, long, std::greater<int>> bids;
    std::map<int, long> asks;
    std::unordered_map<uint64_t, Order> orders;
public:
    void add_order(uint64_t id, Side side, int price, long qty) {
        if(side == Side::Buy) {
            bids[price] += qty;
        } else {
            asks[price] += qty;
        }
        orders.emplace(id, Order{qty, price, side});        
    }

    bool cancel_order(uint64_t id) {
        auto order = orders.find(id);
        if(order == orders.end()) return false;

        auto [qty, price, side] = order->second;

        if(side == Side::Sell) {
            auto it = asks.find(price);
            it->second -= qty;
            if(!it->second) asks.erase(it);
        } else {
            auto it = bids.find(price);
            it->second -= qty;
            if(!it->second) bids.erase(it);
        }

        orders.erase(order);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};

        if(asks.size()) {
            auto [price, qty] = *asks.begin();
            bbo.ask_price = price;
            bbo.ask_qty = qty;
            bbo.has_ask = true;
        }

        if(bids.size()) {
            auto [price, qty] = *bids.begin();
            bbo.bid_price = price;
            bbo.bid_qty = qty;
            bbo.has_bid = true;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        int curr = 0;
        std::vector<Level> res(depth);
        for(auto it = bids.begin(); it != bids.end() && curr < depth; it++, curr++) {
            res[curr] = {it->first, it->second};
        }
        res.resize(curr);
        return res;
    }
    
    std::vector<Level> get_asks(int depth = 10) const {
        int curr = 0;
        std::vector<Level> res(depth);
        for(auto it = asks.begin(); it != asks.end() && curr < depth; it++, curr++) {
            res[curr] = {it->first, it->second};
        }
        res.resize(curr);
        return res;
    }

    BookA() {
        orders.reserve(1000000);
    }
};