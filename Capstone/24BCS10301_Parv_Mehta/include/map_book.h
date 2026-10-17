#pragma once
// Implementation A: std::map for price levels, std::unordered_map for orders.
// Best price is begin() of the map (bids sorted high to low).

#include <functional>
#include <map>
#include <unordered_map>

#include "order_book.h"

class MapBook {
public:
    static const char* name() { return "std::map levels + std::unordered_map orders"; }

    void add_order(uint64_t id, Side side, double price, long qty);
    bool cancel_order(uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

    size_t order_count() const { return orders_.size(); }

private:
    struct Order {
        Side side;
        Tick tick;
        long qty;
    };

    struct LevelInfo {
        long qty = 0;    // total quantity at this price
        int orders = 0;  // number of orders, so we know when the level is empty
    };

    using BidMap = std::map<Tick, LevelInfo, std::greater<Tick>>;
    using AskMap = std::map<Tick, LevelInfo>;

    template <class M> static void add_to_level(M& m, Tick tick, long qty);
    template <class M> static void remove_from_level(M& m, Tick tick, long qty);
    template <class M> static std::vector<Level> top_levels(const M& m, int depth);

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, Order> orders_;
};
