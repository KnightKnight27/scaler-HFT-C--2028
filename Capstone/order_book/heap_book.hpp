#pragma once

#include <functional>
#include <queue>
#include <unordered_map>

#include "common.hpp"

// Implementation B: priority queue with lazy deletion, hash maps for lookups.
//   levels: unordered_map price -> {qty, in_heap}, plus a heap of prices per side
//   orders: unordered_map id -> Order
// Cancelling never touches the heap. A price whose qty hit 0 stays in the heap
// as a dead entry and is popped only when it reaches the top.
class HeapBook {
public:
    void add_order(uint64_t id, Side side, Price price, long qty) {
        if (!valid_order(side, price, qty)) return;
        if (!orders_.emplace(id, Order{side, price, qty}).second) return;  // id already live
        if (side == Side::Buy) add_qty(bids_, price, qty);
        else add_qty(asks_, price, qty);
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order& o = it->second;
        if (o.side == Side::Buy) bids_.levels[o.price].qty -= o.qty;
        else asks_.levels[o.price].qty -= o.qty;
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (prune(bids_)) {
            b.has_bid = true;
            b.bid_price = bids_.heap.top();
            b.bid_qty = bids_.levels.at(b.bid_price).qty;
        }
        if (prune(asks_)) {
            b.has_ask = true;
            b.ask_price = asks_.heap.top();
            b.ask_qty = asks_.levels.at(b.ask_price).qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

private:
    struct LevelInfo {
        long qty = 0;
        bool in_heap = false;
    };

    template <class Cmp>
    struct SideBook {
        std::unordered_map<Price, LevelInfo> levels;
        std::priority_queue<Price, std::vector<Price>, Cmp> heap;
    };
    using BidSide = SideBook<std::less<Price>>;     // max-heap
    using AskSide = SideBook<std::greater<Price>>;  // min-heap

    template <class S>
    static void add_qty(S& s, Price price, long qty) {
        LevelInfo& li = s.levels[price];
        li.qty += qty;
        if (!li.in_heap) {  // each price is in the heap at most once
            li.in_heap = true;
            s.heap.push(price);
        }
    }

    // Pops dead prices off the top. Returns true if a live best level remains.
    template <class S>
    static bool prune(const S& cs) {
        S& s = const_cast<S&>(cs);
        while (!s.heap.empty()) {
            LevelInfo& li = s.levels[s.heap.top()];
            if (li.qty > 0) return true;
            li.in_heap = false;
            s.heap.pop();
        }
        return false;
    }

    // A heap only exposes its top, so copy it and pop until depth live levels are found.
    template <class S>
    static std::vector<Level> top(const S& s, int depth) {
        std::vector<Level> out;
        auto copy = s.heap;
        while (!copy.empty() && (int)out.size() < depth) {
            Price p = copy.top();
            copy.pop();
            long q = s.levels.at(p).qty;
            if (q > 0) out.push_back({p, q});
        }
        return out;
    }

    mutable BidSide bids_;
    mutable AskSide asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
