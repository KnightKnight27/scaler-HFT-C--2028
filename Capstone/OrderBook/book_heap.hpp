#pragma once
#include <functional>
#include <queue>
#include <unordered_map>
#include "common.hpp"

// Implementation B: hash maps + binary heaps with lazy deletion
class HeapBook {
    std::unordered_map<uint64_t, Order> orders;
    std::unordered_map<int, long> bid_qty, ask_qty;

    // a tick is pushed every time its level goes from empty to non-empty,
    // so the heap can hold stale (empty) ticks and duplicates; both are skipped on read
    mutable std::priority_queue<int> bid_heap;
    mutable std::priority_queue<int, std::vector<int>, std::greater<int>> ask_heap;

    template <class H>
    static void clean(H& heap, const std::unordered_map<int, long>& levels) {
        while (!heap.empty() && !levels.count(heap.top())) heap.pop();
    }

    template <class H>
    static std::vector<Level> top(H heap, const std::unordered_map<int, long>& levels, int depth) {
        std::vector<Level> out;
        int last = -1;
        while (!heap.empty() && (int)out.size() < depth) {
            int t = heap.top();
            heap.pop();
            if (t == last) continue;
            auto it = levels.find(t);
            if (it == levels.end()) continue;
            out.push_back({to_price(t), it->second});
            last = t;
        }
        return out;
    }

public:
    HeapBook() { orders.reserve(1 << 20); }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int t = to_tick(price);
        if (!valid(side, t) || qty <= 0) return;
        if (!orders.emplace(id, Order{t, qty, side}).second) return;
        if (side == Side::Buy) {
            long& q = bid_qty[t];
            if (q == 0) bid_heap.push(t);
            q += qty;
        } else {
            long& q = ask_qty[t];
            if (q == 0) ask_heap.push(t);
            q += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) return false;
        Order o = it->second;
        orders.erase(it);
        auto& levels = o.side == Side::Buy ? bid_qty : ask_qty;
        auto lv = levels.find(o.tick);
        lv->second -= o.qty;
        if (lv->second == 0) levels.erase(lv);
        return true;
    }

    BBO get_bbo() const {
        clean(bid_heap, bid_qty);
        clean(ask_heap, ask_qty);
        BBO b{false, 0, 0, false, 0, 0};
        if (!bid_heap.empty()) {
            int t = bid_heap.top();
            b.has_bid = true;
            b.bid_price = to_price(t);
            b.bid_qty = bid_qty.at(t);
        }
        if (!ask_heap.empty()) {
            int t = ask_heap.top();
            b.has_ask = true;
            b.ask_price = to_price(t);
            b.ask_qty = ask_qty.at(t);
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        clean(bid_heap, bid_qty);
        return top(bid_heap, bid_qty, depth);
    }
    std::vector<Level> get_asks(int depth = 10) const {
        clean(ask_heap, ask_qty);
        return top(ask_heap, ask_qty, depth);
    }
};
