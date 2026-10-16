// book_a.h — Implementation A: tree levels + chained-hash order store (baseline).
//
// Structure:
//   orders_ : std::unordered_map<uint64_t, OrderInfo>   id -> {side, tick, qty}
//   bids_   : std::map<Tick, LevelData, std::greater>   best (highest) bid at begin()
//   asks_   : std::map<Tick, LevelData>                 best (lowest) ask at begin()
//
// Only level totals are kept, not per-order queues: with no matching, time priority
// never matters, so cancel just subtracts the order's qty from its level.
//
// Invariants (checked by check_invariants(), which is O(N) and used by tests/verify only):
//   - Each stored level's {qty, count} equals the sum over the live orders at that tick.
//   - No stored level has count == 0. A level is erased the moment its last order goes.
//     (qty >= 1 per order, so count == 0 <=> qty == 0; count is kept as a cross-check.)
//   - Every bid tick is in [5000, 9999] and every ask tick in [10001, 15000].
//
// Complexities (L = levels on a side <= 5000, N = live orders):
//   add_order    O(log L) tree insert/find + O(1) average hash insert
//                (hash worst case O(N) on rehash or collisions)
//   cancel_order O(1) average hash find/erase + O(log L) tree find (+ erase if emptied)
//   get_bbo      O(1): begin() of each map (std::map caches its leftmost node)
//   get_bids/asks O(d), d = min(depth, L): walk from begin()
//
// Memory: every order is its own heap node in the hash table and every new level is
// a tree node, so adds/cancels allocate and free, and lookups chase pointers.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "order_book_iface.h"
#include "types.h"

class BookA {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        const Tick tick = price_to_tick(price);
        assert(is_valid_tick(side, tick));
        assert(qty >= 1);

        const bool inserted = orders_.emplace(id, OrderInfo{side, tick, qty}).second;
        assert(inserted && "order ids must be unique");
        (void)inserted;

        if (side == Side::Buy) {
            add_to_level(bids_, tick, qty);
        } else {
            add_to_level(asks_, tick, qty);
        }
    }

    bool cancel_order(uint64_t id) {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return false;  // unknown or already cancelled

        const OrderInfo order = it->second;  // copy before erase invalidates `it`
        orders_.erase(it);

        if (order.side == Side::Buy) {
            remove_from_level(bids_, order.tick, order.qty);
        } else {
            remove_from_level(asks_, order.tick, order.qty);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};  // value-initialised: has_* false, prices/qtys 0 on an empty side
        if (!bids_.empty()) {
            const auto& best = *bids_.begin();
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(best.first);
            bbo.bid_qty = best.second.qty;
        }
        if (!asks_.empty()) {
            const auto& best = *asks_.begin();
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(best.first);
            bbo.ask_qty = best.second.qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

    bool check_invariants() const {
        std::map<Tick, LevelData> expected_bids;
        std::map<Tick, LevelData> expected_asks;
        for (const auto& entry : orders_) {
            const OrderInfo& o = entry.second;
            if (!is_valid_tick(o.side, o.tick) || o.qty < 1) return false;
            LevelData& lvl = (o.side == Side::Buy ? expected_bids : expected_asks)[o.tick];
            lvl.qty += o.qty;
            lvl.count += 1;
        }
        return levels_match(bids_, expected_bids) && levels_match(asks_, expected_asks);
    }

private:
    struct LevelData {
        long qty = 0;    // sum of live order qtys at this tick
        long count = 0;  // number of live orders at this tick
    };

    template <typename LevelMap>
    static void add_to_level(LevelMap& levels, Tick tick, long qty) {
        LevelData& lvl = levels[tick];  // creates an empty level on first use
        lvl.qty += qty;
        lvl.count += 1;
    }

    template <typename LevelMap>
    static void remove_from_level(LevelMap& levels, Tick tick, long qty) {
        const auto it = levels.find(tick);
        assert(it != levels.end() && "a live order's level must exist");
        LevelData& lvl = it->second;
        lvl.qty -= qty;
        lvl.count -= 1;
        assert(lvl.qty >= 0 && lvl.count >= 0);
        if (lvl.count == 0) {
            assert(lvl.qty == 0);
            levels.erase(it);
        }
    }

    // Map iteration order is already best-first (std::greater for bids).
    template <typename LevelMap>
    static std::vector<Level> top_levels(const LevelMap& levels, int depth) {
        assert(depth >= 0);
        std::vector<Level> out;
        if (depth <= 0) return out;
        out.reserve(std::min(static_cast<std::size_t>(depth), levels.size()));
        for (const auto& entry : levels) {
            if (out.size() == static_cast<std::size_t>(depth)) break;
            out.push_back(Level{tick_to_price(entry.first), entry.second.qty});
        }
        return out;
    }

    template <typename LevelMap>
    static bool levels_match(const LevelMap& actual, const std::map<Tick, LevelData>& expected) {
        if (actual.size() != expected.size()) return false;
        for (const auto& entry : actual) {
            const auto it = expected.find(entry.first);
            if (it == expected.end()) return false;  // stored level with no orders
            if (entry.second.qty != it->second.qty || entry.second.count != it->second.count) return false;
        }
        return true;
    }

    std::unordered_map<uint64_t, OrderInfo> orders_;
    std::map<Tick, LevelData, std::greater<Tick>> bids_;
    std::map<Tick, LevelData> asks_;
};

static_assert(implements_order_book<BookA>(), "BookA must satisfy the order book interface");
