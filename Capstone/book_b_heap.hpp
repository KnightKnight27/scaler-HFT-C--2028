// Rohan Ranjan-10428-Capstone project
//
// Implementation B: binary heaps with lazy deletion + std::map for orders
//
// orders      : std::map  id -> {side, tick, qty}              (red-black tree, no hashing)
// price levels: unordered_map tick -> total qty, plus one heap of ticks per side
//               (bids = max-heap, asks = min-heap). each tick is in its heap at most once.
// best price  : top of the heap, O(1)
// cancel      : tree lookup by id, subtract from the level. if a level hits 0 it is NOT
//               removed from the heap right away (that would be O(n)), it is just marked
//               stale (qty 0). stale ticks are popped only when they reach the top
//               = "lazy deletion". if someone adds at a stale tick again, it is revived.
//
// I use std::push_heap / std::pop_heap on a vector instead of std::priority_queue.
// it is the same heap, but get_bids() needs to read the heap array, and
// priority_queue hides it.

#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <unordered_map>

#include "orderbook.hpp"

class BookB {
    struct OrderInfo {
        Side side;
        int  tick;
        long qty;
    };

    std::map<uint64_t, OrderInfo> orders_;
    std::unordered_map<int, long> level_qty_; // qty 0 = stale, still sitting in a heap
    std::vector<int> bid_heap_;               // max-heap (std::less)
    std::vector<int> ask_heap_;               // min-heap (std::greater)

    long qty_at(int tick) const { return level_qty_.find(tick)->second; }

    // pop stale ticks off the top so the top is always a live level
    template <typename Cmp>
    void clean_top(std::vector<int>& heap, Cmp cmp) {
        while (!heap.empty() && qty_at(heap.front()) == 0) {
            level_qty_.erase(heap.front());
            std::pop_heap(heap.begin(), heap.end(), cmp);
            heap.pop_back();
        }
    }

    // walk the heap best-first without changing it:
    // a small priority queue of heap positions, start at the root,
    // each time take the best one and push its two children.
    // a heap child is never better than its parent, so levels come out in order.
    template <typename Cmp>
    std::vector<Level> top_levels(const std::vector<int>& heap, int depth, Cmp cmp) const {
        std::vector<Level> out;
        if (depth <= 0 || heap.empty()) return out;

        auto by_tick = [&](size_t a, size_t b) { return cmp(heap[a], heap[b]); };
        std::priority_queue<size_t, std::vector<size_t>, decltype(by_tick)> next(by_tick);
        next.push(0);
        while (!next.empty() && (int)out.size() < depth) {
            size_t i = next.top();
            next.pop();
            long q = qty_at(heap[i]);
            if (q > 0) out.push_back({to_price(heap[i]), q}); // skip stale levels
            size_t l = 2 * i + 1, r = l + 1;
            if (l < heap.size()) next.push(l);
            if (r < heap.size()) next.push(r);
        }
        return out;
    }

public:
    static const char* name() { return "B (heap + lazy delete + std::map)"; }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!parse_order(side, price, qty, tick)) return;
        if (!orders_.try_emplace(id, OrderInfo{side, tick, qty}).second) return; // id already live

        auto [it, is_new] = level_qty_.try_emplace(tick, 0);
        it->second += qty;
        if (!is_new) return; // level already in the heap (live or stale), nothing to push

        if (side == Side::Buy) {
            bid_heap_.push_back(tick);
            std::push_heap(bid_heap_.begin(), bid_heap_.end(), std::less<int>());
        } else {
            ask_heap_.push_back(tick);
            std::push_heap(ask_heap_.begin(), ask_heap_.end(), std::greater<int>());
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false; // unknown or already cancelled
        OrderInfo o = it->second;
        orders_.erase(it);

        long& q = level_qty_.find(o.tick)->second;
        q -= o.qty;
        if (q == 0) { // level is empty now, only clean up if it was the best one
            if (o.side == Side::Buy) clean_top(bid_heap_, std::less<int>());
            else                     clean_top(ask_heap_, std::greater<int>());
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bid_heap_.empty()) {
            b.has_bid   = true;
            b.bid_price = to_price(bid_heap_.front());
            b.bid_qty   = qty_at(bid_heap_.front());
        }
        if (!ask_heap_.empty()) {
            b.has_ask   = true;
            b.ask_price = to_price(ask_heap_.front());
            b.ask_qty   = qty_at(ask_heap_.front());
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bid_heap_, depth, std::less<int>()); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(ask_heap_, depth, std::greater<int>()); }
};
