#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "common.hpp"

// Implementation B: priority_queue for best price, with lazy deletion.
// When a level becomes empty its price stays in the heap and gets
// thrown away later when it reaches the top.
class HeapBook {
public:
    void add_order(uint64_t id, Side side, Price price, long qty) {
        if (!valid_order(side, price, qty)) return;
        if (orders.count(id)) return;

        orders[id] = {side, price, qty};
        if (side == Side::Buy) add_qty(bid_levels, bid_heap, price, qty);
        else add_qty(ask_levels, ask_heap, price, qty);
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) return false;

        Order o = it->second;
        orders.erase(it);
        // only the qty goes down here, the heap is cleaned later
        if (o.side == Side::Buy) bid_levels[o.price].qty -= o.qty;
        else ask_levels[o.price].qty -= o.qty;
        return true;
    }

    BBO get_bbo() const {
        clean_top(bid_levels, bid_heap);
        clean_top(ask_levels, ask_heap);

        BBO b;
        if (!bid_heap.empty()) {
            b.has_bid = true;
            b.bid_price = bid_heap.top();
            b.bid_qty = bid_levels.at(b.bid_price).qty;
        }
        if (!ask_heap.empty()) {
            b.has_ask = true;
            b.ask_price = ask_heap.top();
            b.ask_qty = ask_levels.at(b.ask_price).qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bid_levels, bid_heap, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(ask_levels, ask_heap, depth); }

private:
    struct LevelInfo {
        long qty = 0;
        bool in_heap = false;  // so the same price is never pushed twice
    };

    using MaxHeap = std::priority_queue<Price>;
    using MinHeap = std::priority_queue<Price, std::vector<Price>, std::greater<Price>>;
    using LevelMap = std::unordered_map<Price, LevelInfo>;

    std::unordered_map<uint64_t, Order> orders;

    // mutable because get_bbo() pops the dead prices off the top
    mutable LevelMap bid_levels;
    mutable LevelMap ask_levels;
    mutable MaxHeap bid_heap;
    mutable MinHeap ask_heap;

    template <class H>
    static void add_qty(LevelMap& levels, H& heap, Price price, long qty) {
        LevelInfo& lv = levels[price];
        lv.qty += qty;
        if (!lv.in_heap) {
            heap.push(price);
            lv.in_heap = true;
        }
    }

    template <class H>
    static void clean_top(LevelMap& levels, H& heap) {
        while (!heap.empty()) {
            auto it = levels.find(heap.top());
            if (it->second.qty > 0) break;
            levels.erase(it);
            heap.pop();
        }
    }

    // a heap can only give the top, so copy it and pop until we have enough
    template <class H>
    static std::vector<Level> top_levels(const LevelMap& levels, const H& heap, int depth) {
        std::vector<Level> out;
        H copy = heap;
        while (!copy.empty() && (int)out.size() < depth) {
            Price p = copy.top();
            copy.pop();
            long qty = levels.at(p).qty;
            if (qty > 0) out.push_back({p, qty});
        }
        return out;
    }
};
