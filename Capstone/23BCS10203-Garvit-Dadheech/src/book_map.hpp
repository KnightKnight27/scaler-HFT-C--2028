// Implementation A: std::map of price levels, std::list of orders inside each
// level, std::unordered_map from id to the list iterator for cancels.
// Bids are a map with std::greater so begin() is the best bid.
#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "common.hpp"

class MapBook {
public:
    static constexpr const char* name() {
        return "A: std::map<tick, std::list<Order>> + std::unordered_map<id, iterator>";
    }

    OB_API void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ob::to_tick(price);
        if (side == Side::Buy) {
            add_to(bids_, id, side, tick, qty);
        } else {
            add_to(asks_, id, side, tick, qty);
        }
    }

    OB_API bool cancel_order(uint64_t id) {
        auto it = handles_.find(id);
        if (it == handles_.end()) return false;

        const Handle h = it->second;
        handles_.erase(it);

        if (h.side == Side::Buy) {
            remove_from(bids_, h);
        } else {
            remove_from(asks_, h);
        }
        return true;
    }

    OB_API BBO get_bbo() const {
        BBO out = ob::empty_bbo();
        if (!bids_.empty()) {
            const auto& [tick, level] = *bids_.begin();
            out.has_bid = true;
            out.bid_price = ob::to_price(tick);
            out.bid_qty = level.total_qty;
        }
        if (!asks_.empty()) {
            const auto& [tick, level] = *asks_.begin();
            out.has_ask = true;
            out.ask_price = ob::to_price(tick);
            out.ask_qty = level.total_qty;
        }
        return out;
    }

    OB_API std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    OB_API std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

private:
    struct Order {
        uint64_t id;
        long qty;
    };

    struct PriceLevel {
        long total_qty = 0;
        std::list<Order> orders;
    };

    using BidMap = std::map<int, PriceLevel, std::greater<int>>;
    using AskMap = std::map<int, PriceLevel, std::less<int>>;

    struct Handle {
        Side side;
        int tick;
        std::list<Order>::iterator it;
    };

    template <class Map>
    void add_to(Map& book, uint64_t id, Side side, int tick, long qty) {
        PriceLevel& level = book[tick];  // creates the level if needed
        level.total_qty += qty;
        level.orders.push_back(Order{id, qty});
        handles_.emplace(id, Handle{side, tick, std::prev(level.orders.end())});
    }

    template <class Map>
    void remove_from(Map& book, const Handle& h) {
        auto lvl = book.find(h.tick);
        PriceLevel& level = lvl->second;
        level.total_qty -= h.it->qty;
        level.orders.erase(h.it);
        if (level.orders.empty()) book.erase(lvl);
    }

    template <class Map>
    static std::vector<Level> top_levels(const Map& book, int depth) {
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(static_cast<size_t>(depth));
        for (auto it = book.begin(); it != book.end() && depth > 0; ++it, --depth) {
            out.push_back(Level{ob::to_price(it->first), it->second.total_qty});
        }
        return out;
    }

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, Handle> handles_;
};
