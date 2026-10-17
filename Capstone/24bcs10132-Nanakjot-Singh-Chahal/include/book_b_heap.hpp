// Implementation B: binary heaps with lazy deletion for the best price,
// a hash map for per-level quantity, and a std::map (tree) keyed by order id.
//
//   heaps  : binary heap of ticks per side (std::vector + std::push_heap/pop_heap;
//            same structure as std::priority_queue, but we need to copy it for depth)
//   levels : std::unordered_map<tick, total_qty>
//   orders : std::map<id, OrderInfo>
//
// Lazy deletion: when a level empties we do NOT search the heap for it.
// The stale tick stays in the heap and is popped only when it reaches the top.
// An "in_heap" flag per tick stops the same price being pushed twice, so the
// heap never holds more than one entry per price (<= 5000 entries per side).
#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <unordered_map>

#include "common.hpp"

class HeapOrderBook {
public:
    static constexpr const char* kName = "B (binary heap lazy-delete + std::map ids)";

    HeapOrderBook() { in_heap_.fill(false); }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (qty <= 0 || !ticks::valid(side, price, tick)) return;
        if (!orders_.emplace(id, OrderInfo{tick, side, qty}).second) return;
        Half& h = half(side);
        h.qty[tick] += qty;
        if (!in_heap_[tick - ticks::kMin]) {
            in_heap_[tick - ticks::kMin] = true;
            h.push(tick);
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const OrderInfo o = it->second;
        orders_.erase(it);
        Half& h = half(o.side);
        auto lv = h.qty.find(o.tick);
        lv->second -= o.qty;
        if (lv->second == 0) {
            h.qty.erase(lv);
            clean_top(h);  // keep top() valid so get_bbo stays O(1) and const
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0.0, 0, false, 0.0, 0};
        if (!bids_.empty()) {
            const int t = bids_.top();
            b.has_bid = true;
            b.bid_price = ticks::to_price(t);
            b.bid_qty = bids_.qty.find(t)->second;
        }
        if (!asks_.empty()) {
            const int t = asks_.top();
            b.has_ask = true;
            b.ask_price = ticks::to_price(t);
            b.ask_qty = asks_.qty.find(t)->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

private:
    struct Half {
        bool is_bid;
        std::vector<int> heap;  // managed with std::push_heap / std::pop_heap
        std::unordered_map<int, long> qty;

        // std heap algorithms build a max-heap w.r.t. the comparator, so for
        // bids we use less<> (largest on top) and for asks greater<> (smallest on top).
        bool cmp(int a, int b) const { return is_bid ? a < b : a > b; }
        void push(int t) {
            heap.push_back(t);
            std::push_heap(heap.begin(), heap.end(), [this](int a, int b) { return cmp(a, b); });
        }
        void pop() {
            std::pop_heap(heap.begin(), heap.end(), [this](int a, int b) { return cmp(a, b); });
            heap.pop_back();
        }
        int top() const { return heap.front(); }
        bool empty() const { return heap.empty(); }
    };

    Half& half(Side s) { return s == Side::Buy ? bids_ : asks_; }

    void clean_top(Half& h) {
        while (!h.empty() && h.qty.find(h.top()) == h.qty.end()) {
            in_heap_[h.top() - ticks::kMin] = false;
            h.pop();
        }
    }

    // A heap is only ordered at the top, so to read the best `depth` levels
    // we pop from a copy, skipping stale (empty) prices.
    static std::vector<Level> top_levels(const Half& h, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(depth);
        Half copy = Half{h.is_bid, h.heap, {}};
        while (!copy.empty() && static_cast<int>(out.size()) < depth) {
            const int t = copy.top();
            copy.pop();
            auto it = h.qty.find(t);
            if (it != h.qty.end()) out.push_back({ticks::to_price(t), it->second});
        }
        return out;
    }

    Half bids_{true, {}, {}};
    Half asks_{false, {}, {}};
    std::array<bool, ticks::kCount> in_heap_;
    std::map<uint64_t, OrderInfo> orders_;
};
