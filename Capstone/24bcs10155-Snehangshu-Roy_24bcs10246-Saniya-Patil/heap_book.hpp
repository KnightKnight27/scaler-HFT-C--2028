#pragma once

// Implementation B: binary heaps with lazy deletion.
//   best price   -> std::priority_queue of ticks (max-heap for bids, min-heap for asks)
//   level qty    -> std::unordered_map<tick, LevelInfo>
//   orders       -> std::map<id, Order> (ordered tree, so cancel lookup is O(log n))
//
// The heap can't delete from the middle, so when a level empties we leave
// its tick in the heap ("stale") and only pop it once it reaches the top.
// Each tick is pushed at most once (in_heap flag), so the heap never holds
// more than 5000 entries per side. When a cancel empties the level at the
// top we pop stale ticks until the top is live again, so the top of the heap
// is always a live level and get_bbo() stays O(1).

#include <functional>
#include <map>
#include <queue>
#include <unordered_map>

#include "common.hpp"

class HeapBook {
public:
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!valid_qty(qty) || !price_to_tick(side, price, tick)) return false;

        auto res = orders_.emplace(id, Order{side, tick, qty});
        if (!res.second) return false;

        if (side == Side::Buy)
            add_qty(bid_heap_, bid_levels_, tick, qty);
        else
            add_qty(ask_heap_, ask_levels_, tick, qty);
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        if (o.side == Side::Buy)
            remove_qty(bid_heap_, bid_levels_, o.tick, o.qty);
        else
            remove_qty(ask_heap_, ask_levels_, o.tick, o.qty);

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bid_heap_.empty()) {
            int t = bid_heap_.top();
            b.has_bid = true;
            b.bid_price = to_price(t);
            b.bid_qty = bid_levels_.find(t)->second.qty;
        }
        if (!ask_heap_.empty()) {
            int t = ask_heap_.top();
            b.has_ask = true;
            b.ask_price = to_price(t);
            b.ask_qty = ask_levels_.find(t)->second.qty;
        }
        return b;
    }

    // heap isn't sorted, so for depth we copy it and keep popping until we
    // have depth live levels (stale ones get skipped)
    std::vector<Level> get_bids(int depth = 10) const { return top(bid_heap_, bid_levels_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(ask_heap_, ask_levels_, depth); }

    size_t order_count() const { return orders_.size(); }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    struct LevelInfo {
        long qty = 0;
        bool in_heap = false;
    };

    using LevelTable = std::unordered_map<int, LevelInfo>;

    std::priority_queue<int> bid_heap_;
    std::priority_queue<int, std::vector<int>, std::greater<int>> ask_heap_;
    LevelTable bid_levels_;
    LevelTable ask_levels_;
    std::map<uint64_t, Order> orders_;

    template <class Heap>
    static void add_qty(Heap& heap, LevelTable& levels, int tick, long qty) {
        LevelInfo& lv = levels[tick];
        lv.qty += qty;
        if (!lv.in_heap) {
            heap.push(tick);
            lv.in_heap = true;
        }
    }

    template <class Heap>
    static void remove_qty(Heap& heap, LevelTable& levels, int tick, long qty) {
        LevelInfo& lv = levels.find(tick)->second;
        lv.qty -= qty;
        if (lv.qty == 0 && heap.top() == tick) clean_top(heap, levels);
    }

    // pop stale ticks until the top is a level that still has orders
    template <class Heap>
    static void clean_top(Heap& heap, LevelTable& levels) {
        while (!heap.empty()) {
            LevelInfo& lv = levels.find(heap.top())->second;
            if (lv.qty > 0) break;
            lv.in_heap = false;
            heap.pop();
        }
    }

    template <class Heap>
    static std::vector<Level> top(const Heap& heap, const LevelTable& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        Heap copy = heap;
        while (!copy.empty() && (int)out.size() < depth) {
            int t = copy.top();
            copy.pop();
            long q = levels.find(t)->second.qty;
            if (q > 0) out.push_back({to_price(t), q});
        }
        return out;
    }
};
