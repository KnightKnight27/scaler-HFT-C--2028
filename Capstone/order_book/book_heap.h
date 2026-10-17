#pragma once
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>
#include "common.h"

// Implementation B: heap with lazy deletion
//
// std::priority_queue is a heap stored inside a vector, so it is a flat array
// top() is the max (or the min if we give it std::greater) in O(1)
// push and pop are log n
// but it can not delete something from the middle
//
// std::unordered_map is a hash map. find, insert and erase are O(1) on average
//
// orders: hash map from order id to Order
// levels: hash map from tick to total qty, plus one heap of ticks per side
// best price: top of the heap
//
// lazy deletion like we did in class:
// when a level goes to 0 we do not remove it from the heap
// we leave it there as a dead entry and only throw it away when it reaches the top
//
// rule: a tick is inside a heap only if it has an entry in level_qty_
// that way the same tick is never pushed two times
class HeapOrderBook {
public:
    void add_order(uint64_t id, Side side, int32_t tick, long qty) {
        if (!valid_order(side, tick, qty)) {
            return;
        }
        if (!orders_.emplace(id, Order{qty, tick, side}).second) {
            return;  // id already live
        }
        std::unordered_map<int32_t, long>::iterator it = level_qty_.find(tick);
        if (it == level_qty_.end()) {
            // new price, first time in the heap
            level_qty_.emplace(tick, qty);
            if (side == Side::Buy) {
                bid_heap_.push(tick);
            } else {
                ask_heap_.push(tick);
            }
        } else {
            // price is already in the heap, maybe as a dead entry with qty 0
            // adding qty makes it alive again so no push is needed
            it->second += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        std::unordered_map<uint64_t, Order>::iterator it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }
        Order o = it->second;
        orders_.erase(it);

        long& level = level_qty_.find(o.tick)->second;
        level -= o.qty;
        if (level == 0) {
            // the level is dead now. if it is not on top we just leave it (lazy)
            // if it is on top we clean the top right now so get_bbo stays O(1) and const
            if (o.side == Side::Buy) {
                clean_top(bid_heap_);
            } else {
                clean_top(ask_heap_);
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b = {false, 0, 0, false, 0, 0};
        // cancel always cleans dead tops, so the top here is always alive
        if (!bid_heap_.empty()) {
            b.has_bid = true;
            b.bid_tick = bid_heap_.top();
            b.bid_qty = level_qty_.find(b.bid_tick)->second;
        }
        if (!ask_heap_.empty()) {
            b.has_ask = true;
            b.ask_tick = ask_heap_.top();
            b.ask_qty = level_qty_.find(b.ask_tick)->second;
        }
        return b;
    }

    // a heap only knows its top, so to see the next levels we copy it and pop
    // this is the slow part of this design
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        BidHeap copy = bid_heap_;
        while (!copy.empty() && (int)out.size() < depth) {
            long qty = level_qty_.find(copy.top())->second;
            if (qty > 0) {
                out.push_back(Level{copy.top(), qty});
            }
            copy.pop();
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        AskHeap copy = ask_heap_;
        while (!copy.empty() && (int)out.size() < depth) {
            long qty = level_qty_.find(copy.top())->second;
            if (qty > 0) {
                out.push_back(Level{copy.top(), qty});
            }
            copy.pop();
        }
        return out;
    }

private:
    typedef std::priority_queue<int32_t> BidHeap;  // max heap, top is the highest bid
    // std::greater turns it into a min heap, top is the lowest ask
    typedef std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>> AskHeap;

    // pops dead ticks from the top until the top is alive or the heap is empty
    // this is a template only so the same code works for both heap types
    template <typename Heap>
    void clean_top(Heap& heap) {
        while (!heap.empty()) {
            std::unordered_map<int32_t, long>::iterator it = level_qty_.find(heap.top());
            if (it->second != 0) {
                break;
            }
            level_qty_.erase(it);
            heap.pop();
        }
    }

    std::unordered_map<uint64_t, Order> orders_;
    std::unordered_map<int32_t, long> level_qty_;  // bids and asks never share a tick so one map is enough
    BidHeap bid_heap_;
    AskHeap ask_heap_;
};
