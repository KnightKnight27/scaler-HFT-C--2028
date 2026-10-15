// Implementation A: balanced tree of price levels (std::map) + FIFO std::list
// of orders per level + std::unordered_map from order id to the order's node.
#pragma once

#include <algorithm>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <unordered_map>

#include "common.hpp"

class OrderBookA {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        book::Tick tick;
        if (!book::to_valid_tick(side, price, qty, tick)) return;
        if (index_.count(id)) return;  // ids are unique; ignore duplicates

        auto it = side == Side::Buy ? insert(bids_, tick, id, qty) : insert(asks_, tick, id, qty);
        index_.emplace(id, Locator{side, tick, it});
    }

    bool cancel_order(uint64_t id) {
        auto found = index_.find(id);
        if (found == index_.end()) return false;
        const Locator& loc = found->second;
        if (loc.side == Side::Buy)
            erase(bids_, loc);
        else
            erase(asks_, loc);
        index_.erase(found);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_price = book::to_price(bids_.begin()->first);
            b.bid_qty = bids_.begin()->second.total;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_price = book::to_price(asks_.begin()->first);
            b.ask_qty = asks_.begin()->second.total;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top(asks_, depth); }

private:
    struct Order {
        uint64_t id;
        long qty;
    };
    using OrderList = std::list<Order>;

    struct PriceLevel {
        long total = 0;
        OrderList orders;  // arrival order = time priority
    };

    struct Locator {
        Side side;
        book::Tick tick;
        OrderList::iterator it;
    };

    // Bids sorted high->low and asks low->high, so begin() is always the best price.
    std::map<book::Tick, PriceLevel, std::greater<book::Tick>> bids_;
    std::map<book::Tick, PriceLevel, std::less<book::Tick>> asks_;
    std::unordered_map<uint64_t, Locator> index_;

    template <class Ladder>
    static OrderList::iterator insert(Ladder& ladder, book::Tick tick, uint64_t id, long qty) {
        PriceLevel& level = ladder[tick];
        level.total += qty;
        level.orders.push_back(Order{id, qty});
        return std::prev(level.orders.end());
    }

    template <class Ladder>
    static void erase(Ladder& ladder, const Locator& loc) {
        auto lvl = ladder.find(loc.tick);
        lvl->second.total -= loc.it->qty;
        lvl->second.orders.erase(loc.it);
        if (lvl->second.orders.empty()) ladder.erase(lvl);
    }

    template <class Ladder>
    static std::vector<Level> top(const Ladder& ladder, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(std::min<size_t>(depth, ladder.size()));
        for (auto it = ladder.begin(); it != ladder.end() && (int)out.size() < depth; ++it)
            out.push_back(Level{book::to_price(it->first), it->second.total});
        return out;
    }
};
