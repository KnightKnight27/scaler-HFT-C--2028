#pragma once
// Implementation B: binary heap of prices with lazy deletion.
//
// Each side has a heap of the prices that currently have orders (max-heap
// for bids, min-heap for asks), a hash map from price to level, and the
// orders of a level sit in a small vector inside that level. A global hash
// map from id to price tells cancel_order which level to look in.
//
// Lazy deletion: a level that becomes empty is not searched for in the heap.
// It is left there and dropped when it reaches the top. cancel_order cleans
// the top right away, so the top of the heap is always a live level and
// get_bbo never has to do any work.

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include "order_book.h"

class HeapBook {
public:
    static const char* name() { return "price heap (lazy delete) + hashed levels, orders inside levels"; }

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

    size_t order_count() const { return index_.size(); }

private:
    struct OrderEntry {
        uint64_t id;
        long qty;
    };

    struct LevelInfo {
        long qty = 0;
        bool in_heap = false;
        std::vector<OrderEntry> orders;
    };

    // Compare is std::less for bids (max-heap) and std::greater for asks (min-heap).
    template <class Compare>
    struct SideBook {
        std::unordered_map<Tick, LevelInfo> levels;
        std::vector<Tick> heap;
        Compare cmp;

        void push(Tick t) {
            heap.push_back(t);
            std::push_heap(heap.begin(), heap.end(), cmp);
        }

        void pop() {
            std::pop_heap(heap.begin(), heap.end(), cmp);
            heap.pop_back();
        }

        // Throw away empty levels until the top is a live one.
        void pop_empty_top() {
            while (!heap.empty()) {
                auto it = levels.find(heap.front());
                if (it->second.qty > 0) break;
                levels.erase(it);
                pop();
            }
        }
    };

    template <class SB> static void add_impl(SB& sb, Tick tick, uint64_t id, long qty);
    template <class SB> static void remove_impl(SB& sb, Tick tick, uint64_t id);
    template <class SB> static std::vector<Level> top_levels(const SB& sb, int depth);

    SideBook<std::less<Tick>> bids_;
    SideBook<std::greater<Tick>> asks_;
    std::unordered_map<uint64_t, Tick> index_;
};
