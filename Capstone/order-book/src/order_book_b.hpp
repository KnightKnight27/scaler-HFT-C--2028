// Implementation B: binary heap of prices (std::priority_queue) with lazy
// deletion + hash map of price -> aggregate qty + hash map of order id -> order.
//
// A heap cannot delete from the middle, so when a level empties its price is
// left in the heap ("stale") and only removed once it reaches the top. Cancels
// prune stale prices off the top immediately, so the top is always a live level
// and get_bbo() stays O(1) and const.
#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "common.hpp"

class OrderBookB {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        book::Tick tick;
        if (!book::to_valid_tick(side, price, qty, tick)) return;
        if (!orders_.emplace(id, OrderInfo{side, tick, qty}).second) return;  // duplicate id

        if (side == Side::Buy)
            bids_.add(tick, qty);
        else
            asks_.add(tick, qty);
    }

    bool cancel_order(uint64_t id) {
        auto found = orders_.find(id);
        if (found == orders_.end()) return false;
        const OrderInfo& o = found->second;
        if (o.side == Side::Buy)
            bids_.remove(o.tick, o.qty);
        else
            asks_.remove(o.tick, o.qty);
        orders_.erase(found);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.heap.empty()) {
            b.has_bid = true;
            b.bid_price = book::to_price(bids_.heap.top());
            b.bid_qty = bids_.qty_at(bids_.heap.top());
        }
        if (!asks_.heap.empty()) {
            b.has_ask = true;
            b.ask_price = book::to_price(asks_.heap.top());
            b.ask_qty = asks_.qty_at(asks_.heap.top());
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return bids_.top(depth); }
    std::vector<Level> get_asks(int depth = 10) const { return asks_.top(depth); }

private:
    struct OrderInfo {
        Side side;
        book::Tick tick;
        long qty;
    };

    // One side of the book. Invariant: a price has an entry in `levels` iff it
    // is in `heap` (exactly once). Entries with qty == 0 are stale.
    // Cmp = std::less gives a max-heap (bids), std::greater a min-heap (asks).
    template <class Cmp>
    struct HeapSide {
        std::priority_queue<book::Tick, std::vector<book::Tick>, Cmp> heap;
        std::unordered_map<book::Tick, long> levels;

        void add(book::Tick tick, long qty) {
            auto [it, inserted] = levels.try_emplace(tick, 0);
            it->second += qty;
            if (inserted) heap.push(tick);  // a stale entry is simply revived
        }

        void remove(book::Tick tick, long qty) {
            levels.find(tick)->second -= qty;
            // Lazy deletion: drop empty levels only once they surface at the top.
            while (!heap.empty()) {
                auto it = levels.find(heap.top());
                if (it->second > 0) break;
                levels.erase(it);
                heap.pop();
            }
        }

        long qty_at(book::Tick tick) const { return levels.find(tick)->second; }

        // A heap is not sorted, so walk a copy, skipping stale levels.
        std::vector<Level> top(int depth) const {
            std::vector<Level> out;
            if (depth <= 0) return out;
            auto copy = heap;
            while (!copy.empty() && (int)out.size() < depth) {
                const book::Tick t = copy.top();
                copy.pop();
                const long q = qty_at(t);
                if (q > 0) out.push_back(Level{book::to_price(t), q});
            }
            return out;
        }
    };

    HeapSide<std::less<book::Tick>> bids_;
    HeapSide<std::greater<book::Tick>> asks_;
    std::unordered_map<uint64_t, OrderInfo> orders_;
};
