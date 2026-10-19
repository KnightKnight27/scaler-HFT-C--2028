// Implementation B: unordered_map for orders, unordered_map tick -> qty for
// levels, and a priority_queue per side that only answers "best price".
// Levels are never removed from the heap; a stale top is popped lazily when
// it is seen.  The heaps are mutable so get_bbo() can do that while const.
#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "common.hpp"

class HeapBook {
public:
    static constexpr const char* name() {
        return "B: std::unordered_map (orders + levels) + std::priority_queue with lazy deletion";
    }

    HeapBook() {
        orders_.reserve(1 << 16);
        bid_levels_.reserve(8192);
        ask_levels_.reserve(8192);
    }

    OB_API void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ob::to_tick(price);
        orders_.emplace(id, Order{side, tick, qty});
        if (side == Side::Buy) {
            add_level_qty(bid_levels_, bid_heap_, tick, qty);
        } else {
            add_level_qty(ask_levels_, ask_heap_, tick, qty);
        }
    }

    OB_API bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const Order o = it->second;
        orders_.erase(it);

        LevelMap& levels = (o.side == Side::Buy) ? bid_levels_ : ask_levels_;
        auto lvl = levels.find(o.tick);
        lvl->second -= o.qty;
        if (lvl->second == 0) levels.erase(lvl);  // heap entry becomes stale
        return true;
    }

    OB_API BBO get_bbo() const {
        BBO out = ob::empty_bbo();
        prune(bid_heap_, bid_levels_);
        prune(ask_heap_, ask_levels_);
        if (!bid_heap_.empty()) {
            const int tick = bid_heap_.top();
            out.has_bid = true;
            out.bid_price = ob::to_price(tick);
            out.bid_qty = bid_levels_.find(tick)->second;
        }
        if (!ask_heap_.empty()) {
            const int tick = ask_heap_.top();
            out.has_ask = true;
            out.ask_price = ob::to_price(tick);
            out.ask_qty = ask_levels_.find(tick)->second;
        }
        return out;
    }

    OB_API std::vector<Level> get_bids(int depth = 10) const {
        prune(bid_heap_, bid_levels_);
        return top_levels(bid_heap_, bid_levels_, depth);
    }

    OB_API std::vector<Level> get_asks(int depth = 10) const {
        prune(ask_heap_, ask_levels_);
        return top_levels(ask_heap_, ask_levels_, depth);
    }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    using LevelMap = std::unordered_map<int, long>;
    using BidHeap = std::priority_queue<int, std::vector<int>, std::less<int>>;     // max
    using AskHeap = std::priority_queue<int, std::vector<int>, std::greater<int>>;  // min

    template <class Heap>
    static void add_level_qty(LevelMap& levels, Heap& heap, int tick, long qty) {
        auto [it, created] = levels.try_emplace(tick, 0L);
        if (created) heap.push(tick);
        it->second += qty;
    }

    // Pop entries whose level no longer exists.  A tick can be pushed again
    // after its level was erased, so duplicates are possible; top_levels()
    // skips them.
    template <class Heap>
    static void prune(Heap& heap, const LevelMap& levels) {
        while (!heap.empty() && levels.find(heap.top()) == levels.end()) heap.pop();
    }

    template <class Heap>
    static std::vector<Level> top_levels(const Heap& heap, const LevelMap& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<size_t>(depth));
        Heap copy = heap;  // O(H) copy, can't pop the real heap
        int last_tick = -1;
        while (!copy.empty() && static_cast<int>(out.size()) < depth) {
            const int tick = copy.top();
            copy.pop();
            if (tick == last_tick) continue;  // duplicate entry
            auto it = levels.find(tick);
            if (it == levels.end()) continue;  // stale entry
            out.push_back(Level{ob::to_price(tick), it->second});
            last_tick = tick;
        }
        return out;
    }

    std::unordered_map<uint64_t, Order> orders_;
    LevelMap bid_levels_;
    LevelMap ask_levels_;
    mutable BidHeap bid_heap_;
    mutable AskHeap ask_heap_;
};
