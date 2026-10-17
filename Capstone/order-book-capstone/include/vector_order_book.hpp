#pragma once
// =============================================================================
// vector_order_book.hpp  –  Implementation B: Sorted-vector order book
// =============================================================================
// Data structures
// ---------------
//   bid_levels_ : std::vector<Level> sorted DESCENDING by price (tick)
//   ask_levels_ : std::vector<Level> sorted ASCENDING  by price (tick)
//   bid_ticks_  : std::vector<int>   parallel tick array for binary search
//   ask_ticks_  : std::vector<int>   parallel tick array for binary search
//
//   order_map_  : std::unordered_map<uint64_t, OrderMeta>
//
// Binary search locates the index of an existing level in O(log L).
// Insertion of a new level requires shifting elements → O(L) worst case.
// Deletion of an empty level requires shifting → O(L) worst case.
//
// Operation complexities
// ----------------------
//   add_order    : O(log L) search + O(L) shift for new levels (amortized)
//   cancel_order : O(log L) search + O(L) shift if level removed
//   get_bbo      : O(1)  — front of each sorted vector
//   get_bids(d)  : O(d)  — copy first d elements
//   get_asks(d)  : O(d)  — copy first d elements
//
// L = number of currently active price levels.
// =============================================================================

#include "order_book.hpp"
#include <unordered_map>

namespace hft {

class VectorOrderBook : public IOrderBook {
public:
    VectorOrderBook() = default;

    void add_order(std::uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(std::uint64_t id) override;
    BBO  get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    struct OrderMeta {
        Side side;
        int  tick;
        long qty;
    };

    // Levels stored as Level{price,qty} plus parallel tick vector for search
    std::vector<Level> bid_levels_;   // descending by tick
    std::vector<int>   bid_ticks_;    // parallel ticks for lower_bound search
    std::vector<Level> ask_levels_;   // ascending by tick
    std::vector<int>   ask_ticks_;    // parallel ticks for lower_bound search

    std::unordered_map<std::uint64_t, OrderMeta> order_map_;

    // Internal helpers
    // find index of tick in descending bid_ticks_ (returns bid_ticks_.size() if not found)
    std::size_t find_bid_index(int tick) const;
    // find index of tick in ascending ask_ticks_
    std::size_t find_ask_index(int tick) const;
};

} // namespace hft
