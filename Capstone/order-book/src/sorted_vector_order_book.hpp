#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common.hpp"

// B: sorted vectors for orders and price levels, with binary search for both.
// Best prices are at the back. Cancelled orders stay with qty 0 until compaction.
class SortedVectorOrderBook {
public:
    explicit SortedVectorOrderBook(std::size_t expected_orders = 0);

    bool add_order(std::uint64_t id, Side side, double price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct LevelEntry {
        Tick tick;
        long qty;
    };

    struct OrderEntry {
        std::uint64_t id;
        Tick tick;
        std::int32_t qty;  // 0 marks a cancelled order (tombstone)
    };

    using Levels = std::vector<LevelEntry>;
    using Orders = std::vector<OrderEntry>;

    // Worse(a, b) is true when tick a is a worse price than tick b:
    // std::less<Tick> for bids, std::greater<Tick> for asks.
    template <class Worse>
    static Levels::iterator find_level(Levels& levels, Tick tick);
    template <class Worse>
    static void add_to_level(Levels& levels, Tick tick, long qty);
    template <class Worse>
    static void remove_from_level(Levels& levels, Tick tick, long qty);
    static std::vector<Level> best_levels(const Levels& levels, int depth);

    Orders::iterator find_order(std::uint64_t id);
    void compact_orders();

    Levels bids_;  // ascending ticks, best bid at the back
    Levels asks_;  // descending ticks, best ask at the back
    Orders orders_;
    std::size_t tombstones_ = 0;
};
