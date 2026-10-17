#pragma once
// impl B: priority_queue of candidate best ticks + lazy deletion, hash map for level qty
#include "order_book_api.hpp"
#include <queue>
#include <unordered_map>
#include <functional>

struct OrderBookHeap {
    // invariant we keep: a tick is in the heap  <=>  it is a key in the lvl map.
    // cancel never erases from the map, so no duplicate pushes. only get_bbo
    // (and the depth walk) ever discover a level is dead and clean it up.
    std::priority_queue<int32_t> bid_heap;                                            // max on top
    std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>> ask_heap;  // min on top
    std::unordered_map<int32_t, long> bid_lvl, ask_lvl;
    std::unordered_map<uint64_t, OrderRec> orders;

    bool add_order(uint64_t id, Side side, double price, long qty) {
        int32_t tick = to_tick(price);
        if (qty <= 0 || !tick_in_band(side, tick)) return false;
        auto ins = orders.emplace(id, OrderRec{tick, static_cast<int32_t>(qty), side, true});
        if (!ins.second) return false;
        if (side == Side::Buy) add_level(bid_heap, bid_lvl, tick, qty);
        else add_level(ask_heap, ask_lvl, tick, qty);
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) return false;
        const OrderRec& o = it->second;
        // just drop the qty, the heap entry becomes stale and gets popped later
        if (o.side == Side::Buy) bid_lvl[o.tick] -= o.qty;
        else ask_lvl[o.tick] -= o.qty;
        orders.erase(it);
        return true;
    }

    // not const: popping stale tops mutates the heap
    BBO get_bbo() {
        BBO b;
        drop_stale(bid_heap, bid_lvl);
        drop_stale(ask_heap, ask_lvl);
        if (!bid_heap.empty()) {
            b.has_bid = true;
            b.bid_price = to_price(bid_heap.top());
            b.bid_qty = bid_lvl.find(bid_heap.top())->second;
        }
        if (!ask_heap.empty()) {
            b.has_ask = true;
            b.ask_price = to_price(ask_heap.top());
            b.ask_qty = ask_lvl.find(ask_heap.top())->second;
        }
        return b;
    }

    // depth copies the heap and pops, O(k log n) + the copy. slow but correct
    std::vector<Level> get_bids(int depth = 10) const { return walk(bid_heap, bid_lvl, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return walk(ask_heap, ask_lvl, depth); }

private:
    template <class Heap>
    static void add_level(Heap& h, std::unordered_map<int32_t, long>& lvl, int32_t tick, long qty) {
        auto it = lvl.find(tick);
        if (it == lvl.end()) {
            lvl.emplace(tick, qty);
            h.push(tick);  // first time we see this level -> becomes a candidate
        } else {
            it->second += qty;  // already a candidate (maybe with qty 0), don't push again
        }
    }

    // pop tops whose level is 0 and forget those levels so a later add re-pushes
    template <class Heap>
    static void drop_stale(Heap& h, std::unordered_map<int32_t, long>& lvl) {
        while (!h.empty()) {
            auto it = lvl.find(h.top());
            if (it->second > 0) break;
            lvl.erase(it);  // stale top, skip
            h.pop();
        }
    }

    template <class Heap>
    static std::vector<Level> walk(const Heap& h, const std::unordered_map<int32_t, long>& lvl, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        size_t want = static_cast<size_t>(depth);
        out.reserve(want < 64 ? want : 64);
        Heap copy = h;
        while (!copy.empty() && out.size() < want) {
            int32_t t = copy.top();
            copy.pop();
            long q = lvl.find(t)->second;
            if (q > 0) out.push_back(Level{to_price(t), q});
        }
        return out;
    }
};
