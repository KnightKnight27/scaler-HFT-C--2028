// book_b.h — Implementation B: flat sorted-vector levels + tree order store.
//
// Structure:
//   orders_ : std::map<uint64_t, OrderInfo>   red-black tree keyed by id
//   bids_   : std::vector<LevelEntry> sorted ascending by tick  -> best (highest) at back()
//   asks_   : std::vector<LevelEntry> sorted descending by tick -> best (lowest)  at back()
//
// Both vectors are ordered "worst to best", so the best level is always back(). Adding or
// removing a level costs a shift of every element after it, i.e. every BETTER level.
// Keeping the best at the back makes changes near the top of the book the cheap ones.
//
// Invariants (checked by check_invariants(), O(N + L), tests/verify only):
//   - Each vector is strictly ordered worst -> best (no duplicate ticks).
//   - Each stored level's {qty, count} equals the sum over the live orders at that tick.
//   - No stored level has count == 0. A level is erased the moment its last order goes.
//   - Every bid tick is in [5000, 9999] and every ask tick in [10001, 15000].
//
// Complexities (L = levels on a side <= 5000, N = live orders):
//   add_order    O(log N) tree insert + O(log L) binary search
//                + O(L) shift if the tick is a new level (O(levels better than it))
//   cancel_order O(log N) tree find/erase + O(log L) binary search
//                + O(L) shift if the level empties
//   get_bbo      O(1): back() of each vector
//   get_bids/asks O(d), d = min(depth, L): walk from the back
//
// Memory: levels are contiguous (two 5,000-entry vectors reserved up front, ~120 KB each
// at most), but every order is a separately allocated tree node, so the id lookup on
// cancel chases ~log2(N) ≈ 19 dependent pointers at N ≈ 500k.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "order_book_iface.h"
#include "types.h"

class BookB {
public:
    BookB() {
        // One side can never hold more than 5,000 levels, so reserving that much means
        // the vectors never reallocate during a run.
        bids_.reserve(kLevelsPerSide);
        asks_.reserve(kLevelsPerSide);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        const Tick tick = price_to_tick(price);
        assert(is_valid_tick(side, tick));
        assert(qty >= 1);

        const bool inserted = orders_.emplace(id, OrderInfo{side, tick, qty}).second;
        assert(inserted && "order ids must be unique");
        (void)inserted;

        if (side == Side::Buy) {
            add_to_level<Side::Buy>(bids_, tick, qty);
        } else {
            add_to_level<Side::Sell>(asks_, tick, qty);
        }
    }

    bool cancel_order(uint64_t id) {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return false;  // unknown or already cancelled

        const OrderInfo order = it->second;  // copy before erase invalidates `it`
        orders_.erase(it);

        if (order.side == Side::Buy) {
            remove_from_level<Side::Buy>(bids_, order.tick, order.qty);
        } else {
            remove_from_level<Side::Sell>(asks_, order.tick, order.qty);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(bids_.back().tick);
            bbo.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(asks_.back().tick);
            bbo.ask_qty = asks_.back().qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const { return top_levels(bids_, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return top_levels(asks_, depth); }

    bool check_invariants() const {
        std::map<Tick, LevelEntry> expected_bids;
        std::map<Tick, LevelEntry> expected_asks;
        for (const auto& entry : orders_) {
            const OrderInfo& o = entry.second;
            if (!is_valid_tick(o.side, o.tick) || o.qty < 1) return false;
            LevelEntry& lvl = (o.side == Side::Buy ? expected_bids : expected_asks)[o.tick];
            lvl.tick = o.tick;
            lvl.qty += o.qty;
            lvl.count += 1;
        }
        return side_matches<Side::Buy>(bids_, expected_bids) && side_matches<Side::Sell>(asks_, expected_asks);
    }

private:
    struct LevelEntry {
        Tick tick = 0;
        long qty = 0;    // sum of live order qtys at this tick
        long count = 0;  // number of live orders at this tick
    };

    static constexpr std::size_t kLevelsPerSide = kMaxBidTick - kMinBidTick + 1;  // 5,000

    // True if price level `a` is worse than `b` for side S (lower bid / higher ask).
    // This is the sort order of the vectors: worst first, best at the back.
    template <Side S>
    static bool worse(Tick a, Tick b) {
        return S == Side::Buy ? a < b : a > b;
    }

    // First entry that is not worse than `tick`: the level itself if it exists,
    // otherwise the position where it must be inserted to keep the order.
    template <Side S>
    static std::vector<LevelEntry>::iterator find_slot(std::vector<LevelEntry>& levels, Tick tick) {
        return std::lower_bound(levels.begin(), levels.end(), tick,
                                [](const LevelEntry& e, Tick t) { return worse<S>(e.tick, t); });
    }

    template <Side S>
    static void add_to_level(std::vector<LevelEntry>& levels, Tick tick, long qty) {
        const auto it = find_slot<S>(levels, tick);
        if (it != levels.end() && it->tick == tick) {
            it->qty += qty;
            it->count += 1;
        } else {
            levels.insert(it, LevelEntry{tick, qty, 1});  // shifts every better level up by one
        }
    }

    template <Side S>
    static void remove_from_level(std::vector<LevelEntry>& levels, Tick tick, long qty) {
        const auto it = find_slot<S>(levels, tick);
        assert(it != levels.end() && it->tick == tick && "a live order's level must exist");
        it->qty -= qty;
        it->count -= 1;
        assert(it->qty >= 0 && it->count >= 0);
        if (it->count == 0) {
            assert(it->qty == 0);
            levels.erase(it);  // shifts every better level down by one
        }
    }

    static std::vector<Level> top_levels(const std::vector<LevelEntry>& levels, int depth) {
        assert(depth >= 0);
        std::vector<Level> out;
        if (depth <= 0) return out;
        const std::size_t n = std::min(static_cast<std::size_t>(depth), levels.size());
        out.reserve(n);
        for (auto it = levels.rbegin(); out.size() < n; ++it) {
            out.push_back(Level{tick_to_price(it->tick), it->qty});
        }
        return out;
    }

    template <Side S>
    static bool side_matches(const std::vector<LevelEntry>& actual, const std::map<Tick, LevelEntry>& expected) {
        if (actual.size() != expected.size()) return false;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const LevelEntry& lvl = actual[i];
            if (i > 0 && !worse<S>(actual[i - 1].tick, lvl.tick)) return false;  // strict order
            const auto it = expected.find(lvl.tick);
            if (it == expected.end()) return false;
            if (lvl.qty != it->second.qty || lvl.count != it->second.count) return false;
        }
        return true;
    }

    std::map<uint64_t, OrderInfo> orders_;
    std::vector<LevelEntry> bids_;  // ascending ticks, best (highest) at back
    std::vector<LevelEntry> asks_;  // descending ticks, best (lowest) at back
};

static_assert(implements_order_book<BookB>(), "BookB must satisfy the order book interface");
