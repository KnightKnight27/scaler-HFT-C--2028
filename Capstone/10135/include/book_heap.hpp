// Implementation B: binary heaps with lazy deletion.
//
// Idea: the only thing a heap is really good at is handing back the extreme
// element, which is exactly what BBO wants. So I keep a max-heap of bid ticks
// and a min-heap of ask ticks. The actual quantity per level lives in a separate
// hash table, and orders live in a std::map (a tree) so this build also differs
// from A in where it keeps the order records.
//
// A heap cannot erase an interior element, so cancellation is lazy: when a level
// empties I just drop it from the quantity table and leave whatever stale tick
// is still sitting in the heap. Before I trust the heap top I pop any entries
// whose tick is no longer active. A tick is only pushed when its level goes from
// empty to non-empty, which keeps the heaps close to the number of live levels
// instead of the number of orders.
#ifndef OB_BOOK_HEAP_HPP
#define OB_BOOK_HEAP_HPP

#include "common.hpp"
#include <queue>
#include <map>
#include <unordered_map>
#include <functional>

class BookHeap {
public:
    BookHeap() {
        bid_qty_.reserve(1 << 13);
        ask_qty_.reserve(1 << 13);
    }

    const char* name() const { return "B (priority_queue + lazy deletion)"; }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        validate_new_order(side, tick, qty);

        auto res = orders_.emplace(id, OrderRec{side, tick, qty});
        if (!res.second) {
            throw std::invalid_argument("duplicate order id");
        }

        if (side == Side::Buy) {
            long& q = bid_qty_[tick];
            if (q == 0) {
                bid_heap_.push(tick);   // level just became active
            }
            q += qty;
        } else {
            long& q = ask_qty_[tick];
            if (q == 0) {
                ask_heap_.push(tick);
            }
            q += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }
        const OrderRec& o = it->second;
        if (o.side == Side::Buy) {
            auto q = bid_qty_.find(o.tick);
            q->second -= o.qty;
            if (q->second == 0) {
                bid_qty_.erase(q);      // lazy: stale tick stays in the heap
            }
        } else {
            auto q = ask_qty_.find(o.tick);
            q->second -= o.qty;
            if (q->second == 0) {
                ask_qty_.erase(q);
            }
        }
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        int tick;
        long qty;
        if (best_bid(tick, qty)) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(tick);
            bbo.bid_qty = qty;
        }
        if (best_ask(tick, qty)) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(tick);
            bbo.ask_qty = qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        scratch_.clear();
        int last = -1;
        // Pop the top entries, skipping stale ticks and duplicates, until we
        // have enough active levels, then put everything back.
        while ((int)out.size() < depth && !bid_heap_.empty()) {
            int t = bid_heap_.top();
            bid_heap_.pop();
            scratch_.push_back(t);
            if (t == last) continue;          // duplicate of a level already taken
            auto q = bid_qty_.find(t);
            if (q == bid_qty_.end() || q->second == 0) continue;  // stale
            out.push_back({tick_to_price(t), q->second});
            last = t;
        }
        for (int t : scratch_) bid_heap_.push(t);
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        out.reserve(depth);
        scratch_.clear();
        int last = -1;
        while ((int)out.size() < depth && !ask_heap_.empty()) {
            int t = ask_heap_.top();
            ask_heap_.pop();
            scratch_.push_back(t);
            if (t == last) continue;
            auto q = ask_qty_.find(t);
            if (q == ask_qty_.end() || q->second == 0) continue;
            out.push_back({tick_to_price(t), q->second});
            last = t;
        }
        for (int t : scratch_) ask_heap_.push(t);
        return out;
    }

private:
    // Clean stale ticks off the top and report the current best bid.
    bool best_bid(int& tick, long& qty) const {
        while (!bid_heap_.empty()) {
            int t = bid_heap_.top();
            auto q = bid_qty_.find(t);
            if (q == bid_qty_.end() || q->second == 0) {
                bid_heap_.pop();
                continue;
            }
            tick = t;
            qty = q->second;
            return true;
        }
        return false;
    }

    bool best_ask(int& tick, long& qty) const {
        while (!ask_heap_.empty()) {
            int t = ask_heap_.top();
            auto q = ask_qty_.find(t);
            if (q == ask_qty_.end() || q->second == 0) {
                ask_heap_.pop();
                continue;
            }
            tick = t;
            qty = q->second;
            return true;
        }
        return false;
    }

    // Heaps and the cleanup scratch buffer are mutable because reading the BBO or
    // a depth snapshot prunes stale entries, which is a logical const operation.
    mutable std::priority_queue<int> bid_heap_;                                  // max-heap
    mutable std::priority_queue<int, std::vector<int>, std::greater<int>> ask_heap_;  // min-heap
    mutable std::vector<int> scratch_;
    std::unordered_map<int, long> bid_qty_;
    std::unordered_map<int, long> ask_qty_;
    std::map<uint64_t, OrderRec> orders_;
};

#endif // OB_BOOK_HEAP_HPP
