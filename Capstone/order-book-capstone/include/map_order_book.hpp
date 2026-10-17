#pragma once
// =============================================================================
// map_order_book.hpp  –  Implementation A: Balanced-tree (std::map) order book
// =============================================================================
// Data structures
// ---------------
//   bids_ : std::map<int, long, std::greater<int>>
//       key   = tick price (integer)
//       value = aggregate quantity at that level
//       std::greater keeps the HIGHEST price at begin() → O(1) best-bid access
//
//   asks_ : std::map<int, long>
//       key   = tick price (integer)
//       value = aggregate quantity
//       Default (less) keeps the LOWEST price at begin() → O(1) best-ask access
//
//   order_map_ : std::unordered_map<uint64_t, OrderMeta>
//       Allows O(1) average lookup for cancel by order ID.
//       OrderMeta stores (side, tick, qty) so we know which level to decrement.
//
// Operation complexities
// ----------------------
//   add_order    : O(log L)  — map insertion/update, L = active price levels
//   cancel_order : O(log L)  — map lookup + possible erase, unordered_map is O(1) avg
//   get_bbo      : O(1)      — map::begin() on both sides
//   get_bids(d)  : O(d)      — iterate first d entries of bids_
//   get_asks(d)  : O(d)      — iterate first d entries of asks_
// =============================================================================

#include "order_book.hpp"
#include <map>
#include <unordered_map>

namespace hft {

class MapOrderBook : public IOrderBook {
public:
    MapOrderBook() = default;

    void add_order(std::uint64_t id, Side side, double price, long qty) override;
    bool cancel_order(std::uint64_t id) override;
    BBO  get_bbo() const override;
    std::vector<Level> get_bids(int depth = 10) const override;
    std::vector<Level> get_asks(int depth = 10) const override;

private:
    struct OrderMeta {
        Side          side;
        int           tick;
        long          qty;
    };

    // bids_ : highest price first
    std::map<int, long, std::greater<int>> bids_;
    // asks_ : lowest price first
    std::map<int, long>                    asks_;
    // order-ID lookup
    std::unordered_map<std::uint64_t, OrderMeta> order_map_;
};

} // namespace hft
