// Implementation B: everything in sorted, contiguous std::vectors (no per-node allocation).
//
//   price levels : std::vector<{tick, qty}> per side, sorted from WORST to BEST price,
//                  so the best price is always back() and inserting/erasing a level near
//                  the top of the book only shifts a few elements
//   orders       : std::vector<{id, tick, side, qty}> sorted by id. Exchange-style ids
//                  arrive in increasing order, so adding is a push_back. Cancelled orders
//                  become tombstones (qty = 0); the vector is compacted when more than
//                  half of it is dead
//   best price   : levels.back()
//   cancel       : binary search over the id-sorted order vector, then binary search over
//                  the price-level vector
#pragma once

#include <functional>

#include "common.hpp"

class OrderBookVector {
public:
    static constexpr const char* kName = "B: sorted std::vector levels + id-sorted std::vector orders";

    bool add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;
    size_t order_count() const { return orders_.size() - dead_; }

private:
    struct Order {
        uint64_t id;
        int32_t tick;
        Side side;
        long qty;  // 0 marks a tombstone (cancelled order)
    };

    struct LevelEntry {
        int32_t tick;
        long qty;
    };

    // One side of the book. `Better(a, b)` is true when tick a is a better price than b.
    // Entries are stored worst -> best, so back() is the best level.
    template <class Better>
    class SortedLevels {
    public:
        void add(int32_t tick, long qty);
        void reduce(int32_t tick, long qty);
        bool empty() const { return v_.empty(); }
        const LevelEntry& best() const { return v_.back(); }
        std::vector<Level> top(int depth) const;

    private:
        std::vector<LevelEntry>::iterator find_pos(int32_t tick);
        std::vector<LevelEntry> v_;
    };

    std::vector<Order>::iterator find_order(uint64_t id);
    void compact();

    SortedLevels<std::greater<int32_t>> bids_;
    SortedLevels<std::less<int32_t>> asks_;
    std::vector<Order> orders_;
    size_t dead_ = 0;  // number of tombstones in orders_
};
