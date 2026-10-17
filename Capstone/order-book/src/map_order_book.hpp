#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "common.hpp"

// A: maps keep the price levels sorted; an unordered_map finds orders by id.
// The best price is the first entry on each side.
class MapOrderBook {
public:
    explicit MapOrderBook(std::size_t expected_orders = 0);

    bool add_order(std::uint64_t id, Side side, double price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct Order {
        Tick tick;
        Side side;
        long qty;
    };

    std::map<Tick, long, std::greater<Tick>> bids_;
    std::map<Tick, long, std::less<Tick>> asks_;
    std::unordered_map<std::uint64_t, Order> orders_;
};
