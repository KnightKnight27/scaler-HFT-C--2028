// oracle.h — deliberately naive reference book. Used only by --verify and the edge
// tests, never benchmarked.
//
// Why it exists: if A, B and C share a bug (e.g. in a shared helper), they all agree
// with each other and "they agree" proves nothing. The oracle is written to be
// obviously correct rather than fast, and avoids the techniques the real books use:
//   - orders  : std::map<id, {side, tick, qty}>
//   - levels  : a plain array of total qty per tick (no order counts, no caching)
//   - best    : linear scan from the best end of the band on EVERY query
//   - ticks   : its own conversion (std::round), not price_to_tick() from types.h
//
// Every precondition is checked unconditionally (even in release builds) and aborts,
// so the oracle also validates the workload generator.
//
// Cost: a query scans up to 5,000 ticks. That is fine for verification. In this
// workload the book fills quickly, so scans usually stop after a few ticks.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "types.h"

class OracleBook {
public:
    OracleBook() : level_qty_(kNumTicks, 0) {}

    void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = to_tick(price);
        const bool in_band = side == Side::Buy ? (tick >= 5000 && tick <= 9999) : (tick >= 10001 && tick <= 15000);
        if (!in_band) fail("add_order: price outside the side's band", id);
        if (qty < 1) fail("add_order: qty < 1", id);
        if (orders_.count(id) != 0) fail("add_order: duplicate id", id);

        orders_[id] = Order{side, tick, qty};
        long& level = level_qty_[static_cast<std::size_t>(tick - 5000)];
        if (level == 0) ++levels_created_;
        level += qty;
    }

    bool cancel_order(uint64_t id) {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        long& level = level_qty_[static_cast<std::size_t>(it->second.tick - 5000)];
        level -= it->second.qty;
        if (level < 0) fail("cancel_order: level qty went negative", id);
        if (level == 0) ++levels_emptied_;
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        const std::vector<Level> bids = get_bids(1);
        const std::vector<Level> asks = get_asks(1);
        if (!bids.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids[0].price;
            bbo.bid_qty = bids[0].qty;
        }
        if (!asks.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks[0].price;
            bbo.ask_qty = asks[0].qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int tick = 9999; tick >= 5000 && static_cast<int>(out.size()) < depth; --tick) {
            const long qty = level_qty_[static_cast<std::size_t>(tick - 5000)];
            if (qty > 0) out.push_back(Level{tick / 100.0, qty});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int tick = 10001; tick <= 15000 && static_cast<int>(out.size()) < depth; ++tick) {
            const long qty = level_qty_[static_cast<std::size_t>(tick - 5000)];
            if (qty > 0) out.push_back(Level{tick / 100.0, qty});
        }
        return out;
    }

    // Recompute every level from the individual orders and compare.
    bool check_invariants() const {
        std::vector<long> expected(kNumTicks, 0);
        for (const auto& entry : orders_) expected[static_cast<std::size_t>(entry.second.tick - 5000)] += entry.second.qty;
        return expected == level_qty_;
    }

    // Statistics about the workload (not part of the book API).
    std::size_t live_orders() const { return orders_.size(); }
    long levels_created() const { return levels_created_; }  // empty -> non-empty transitions
    long levels_emptied() const { return levels_emptied_; }  // non-empty -> empty transitions
    int non_empty_levels(Side side) const {
        int n = 0;
        const int lo = side == Side::Buy ? 5000 : 10001;
        const int hi = side == Side::Buy ? 9999 : 15000;
        for (int tick = lo; tick <= hi; ++tick) n += level_qty_[static_cast<std::size_t>(tick - 5000)] > 0;
        return n;
    }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    static int to_tick(double price) { return static_cast<int>(std::round(price * 100.0)); }

    [[noreturn]] static void fail(const char* what, uint64_t id) {
        std::fprintf(stderr, "ORACLE PRECONDITION FAILED: %s (id=%llu)\n", what, static_cast<unsigned long long>(id));
        std::abort();
    }

    std::map<uint64_t, Order> orders_;
    std::vector<long> level_qty_;  // index = tick - 5000
    long levels_created_ = 0;
    long levels_emptied_ = 0;
};
