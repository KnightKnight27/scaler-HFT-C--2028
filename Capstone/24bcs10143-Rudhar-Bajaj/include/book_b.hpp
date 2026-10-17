#pragma once
// Implementation B: binary heaps with lazy deletion.
//
//   price levels : std::unordered_map<tick, {qty, in_heap}> per side
//   best price   : std::priority_queue<tick> per side (max-heap bids, min-heap asks)
//   orders       : std::map<id, {side, tick, qty}> (ordered tree, unlike A's hash)
//
// A tick is pushed onto its heap only when it is not already in it. When a
// level empties we leave its tick in the heap (lazy deletion) and only pop it
// once it reaches the top. Invariant: the top of each heap is always a live
// level, so get_bbo() is a plain top() read and stays const.
// get_bids/get_asks copy the heap and pop from the copy, skipping dead ticks.

#include <functional>
#include <map>
#include <queue>
#include <unordered_map>

#include "common.hpp"

class BookB {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ticks::from_price(price);
        if (qty <= 0 || !ticks::in_band(side, tick) || !ticks::on_grid(price, tick)) return;
        auto [it, inserted] = orders_.try_emplace(id, Order{side, tick, qty});
        if (!inserted) return;
        if (side == Side::Buy) add_level(bid_levels_, bid_heap_, tick, qty);
        else add_level(ask_levels_, ask_heap_, tick, qty);
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order o = it->second;
        orders_.erase(it);
        if (o.side == Side::Buy) remove_qty(bid_levels_, bid_heap_, o.tick, o.qty);
        else remove_qty(ask_levels_, ask_heap_, o.tick, o.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bid_heap_.empty()) {
            const int t = bid_heap_.top();
            b.has_bid = true;
            b.bid_price = ticks::to_price(t);
            b.bid_qty = bid_levels_.at(t).qty;
        }
        if (!ask_heap_.empty()) {
            const int t = ask_heap_.top();
            b.has_ask = true;
            b.ask_price = ticks::to_price(t);
            b.ask_qty = ask_levels_.at(t).qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bid_levels_, bid_heap_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(ask_levels_, ask_heap_, depth); }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };
    struct LevelB {
        long qty = 0;
        bool in_heap = false;
    };
    using Levels = std::unordered_map<int, LevelB>;
    using BidHeap = std::priority_queue<int>;
    using AskHeap = std::priority_queue<int, std::vector<int>, std::greater<int>>;

    template <class Heap>
    static void add_level(Levels& levels, Heap& heap, int tick, long qty) {
        LevelB& lv = levels[tick];
        lv.qty += qty;
        if (!lv.in_heap) {
            lv.in_heap = true;
            heap.push(tick);
        }
    }

    template <class Heap>
    static void remove_qty(Levels& levels, Heap& heap, int tick, long qty) {
        levels.find(tick)->second.qty -= qty;
        // Pop dead ticks off the top so the top is always live.
        while (!heap.empty()) {
            auto top_it = levels.find(heap.top());
            if (top_it->second.qty > 0) break;
            levels.erase(top_it);
            heap.pop();
        }
    }

    template <class Heap>
    static std::vector<Level> top(const Levels& levels, Heap heap /* copy */, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        while (!heap.empty() && (int)out.size() < depth) {
            const int t = heap.top();
            heap.pop();
            const long q = levels.at(t).qty;
            if (q > 0) out.push_back({ticks::to_price(t), q});
        }
        return out;
    }

    std::map<uint64_t, Order> orders_;
    Levels bid_levels_, ask_levels_;
    BidHeap bid_heap_;
    AskHeap ask_heap_;
};
