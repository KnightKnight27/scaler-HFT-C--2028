#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <cmath>

#include "order_book.hpp"

class OrderBookB {
private:
    struct OrderInfo {
        Side side;
        int price_tick;
        long qty;
        bool active;
    };


    std::unordered_map<uint64_t, OrderInfo> orders_;

    struct BidHeapNode {
        int price_tick;
        uint64_t id;
        bool operator<(const BidHeapNode& other) const {
            
            return price_tick < other.price_tick;
        }
    };

    struct AskHeapNode {
        int price_tick;
        uint64_t id;
        bool operator>(const AskHeapNode& other) const {
            
            return price_tick > other.price_tick;
        }
    };

    mutable std::priority_queue<BidHeapNode> bid_pq_;
    mutable std::priority_queue<AskHeapNode, std::vector<AskHeapNode>, std::greater<AskHeapNode>> ask_pq_;

    
    std::unordered_map<int, long> bid_quantities_;
    std::unordered_map<int, long> ask_quantities_;

    static double tick_to_price(int tick) {
        return static_cast<double>(tick) / 100.0;
    }

   
    void clean_bids() const;
    void clean_asks() const;

public:
    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
};