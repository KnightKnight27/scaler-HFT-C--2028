// map_book.h - Implementation A: red-black tree of price levels + FIFO list per level.
//
//   price levels : std::map<Price, PriceLevel> per side (bids sorted high->low, asks low->high)
//   orders       : std::list<Order> inside each level, in arrival (time-priority) order
//   best price   : the first element of the map, begin()
//   cancel       : std::unordered_map<id, Locator>; the locator keeps an iterator to the
//                  level in the map and to the order in the list, so cancel never searches
//
// Every level, every order and every hash entry is its own heap allocation, so this is
// the "pointer chasing" design.
#pragma once

#include <algorithm>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "common.h"

class MapOrderBook {
public:
    static constexpr const char* kName = "A: std::map levels + std::list orders + std::unordered_map index";

    explicit MapOrderBook(size_t expected_orders = 1 << 16) { index_.reserve(expected_orders); }

    // Returns false (and changes nothing) for a bad price/qty or an id that is already live.
    bool add_order(OrderId id, Side side, Price price, Qty qty) {
        if (!valid_price(side, price) || !valid_qty(qty)) return false;
        auto [it, inserted] = index_.try_emplace(id);
        if (!inserted) return false;

        Locator& loc = it->second;
        loc.side = side;
        if (side == Side::Buy) {
            loc.bid_level = bids_.try_emplace(price).first;
            loc.order = push_order(loc.bid_level->second, id, qty);
        } else {
            loc.ask_level = asks_.try_emplace(price).first;
            loc.order = push_order(loc.ask_level->second, id, qty);
        }
        return true;
    }

    bool cancel_order(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return false;

        const Locator& loc = it->second;
        if (loc.side == Side::Buy) {
            remove_order(bids_, loc.bid_level, loc.order);
        } else {
            remove_order(asks_, loc.ask_level, loc.order);
        }
        index_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = bids_.begin()->first;
            b.bid_qty = bids_.begin()->second.total;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = asks_.begin()->first;
            b.ask_qty = asks_.begin()->second.total;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

private:
    struct Order {
        OrderId id;
        Qty qty;
    };

    struct PriceLevel {
        Qty total = 0;            // sum of qty of the orders below
        std::list<Order> orders;  // oldest first
    };

    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;  // begin() = highest bid
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;     // begin() = lowest ask

    // Where a live order sits. Only the iterator of its own side is used.
    struct Locator {
        Side side;
        BidMap::iterator bid_level;
        AskMap::iterator ask_level;
        std::list<Order>::iterator order;
    };

    static std::list<Order>::iterator push_order(PriceLevel& level, OrderId id, Qty qty) {
        level.total += qty;
        level.orders.push_back(Order{id, qty});
        return std::prev(level.orders.end());
    }

    template <class LevelMap>
    static void remove_order(LevelMap& levels, typename LevelMap::iterator level,
                             std::list<Order>::iterator order) {
        level->second.total -= order->qty;
        level->second.orders.erase(order);
        if (level->second.orders.empty()) levels.erase(level);  // drop empty price levels
    }

    template <class LevelMap>
    static std::vector<Level> top_levels(const LevelMap& levels, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(std::min(depth, kLevelsPerSide));
        for (auto it = levels.begin(); it != levels.end() && static_cast<int>(out.size()) < depth; ++it) {
            out.push_back(Level{it->first, it->second.total});
        }
        return out;
    }

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, Locator> index_;
};
