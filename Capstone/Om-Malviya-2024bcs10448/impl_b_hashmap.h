// impl_b_hashmap.h — Implementation B: unordered_map price levels + cached best.
//
//   Price levels : std::unordered_map<tick, qty> per side. Hashed, so levels are
//                  scattered in memory with no inherent ordering.
//   Orders       : std::unordered_map<id, {side, tick, qty}> for cancellation.
//   Best price   : cached best_bid_tick_ / best_ask_tick_.
//                    - add: O(1) update (improve best if the new level is better).
//                    - cancel that empties the best level: RECOMPUTE by scanning
//                      every key in the side's map -> O(L), cache-unfriendly.
//   get_bbo      : O(1) via the cached best.
//   get_bids/asks: copy all levels + partial_sort the top `depth` -> O(L).
//
// This design deliberately contrasts with A/C: hashing scatters levels across
// memory and the lazy best-recompute does a full scan, which the perf analysis
// expects to show as more cache misses / branch mispredicts.

#ifndef CAPSTONE_IMPL_B_HASHMAP_H
#define CAPSTONE_IMPL_B_HASHMAP_H

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "common.h"

class OrderBookB {
public:
    OrderBookB() {
        orders_.reserve(1 << 20);
        bid_levels_.reserve(1 << 13);
        ask_levels_.reserve(1 << 13);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        const int tick = ticks::to_tick(price);
        orders_.emplace(id, OrderRec{side, tick, qty});
        if (side == Side::Buy) {
            bid_levels_[tick] += qty;
            if (best_bid_ == kNone || tick > best_bid_) best_bid_ = tick;
        } else {
            ask_levels_[tick] += qty;
            if (best_ask_ == kNone || tick < best_ask_) best_ask_ = tick;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        const OrderRec o = it->second;
        orders_.erase(it);
        if (o.side == Side::Buy) {
            if (drain(bid_levels_, o.tick, o.qty) && o.tick == best_bid_)
                best_bid_ = recompute_max(bid_levels_);
        } else {
            if (drain(ask_levels_, o.tick, o.qty) && o.tick == best_ask_)
                best_ask_ = recompute_min(ask_levels_);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ != kNone) {
            b.has_bid = true;
            b.bid_price = ticks::to_price(best_bid_);
            b.bid_qty = bid_levels_.at(best_bid_);
        }
        if (best_ask_ != kNone) {
            b.has_ask = true;
            b.ask_price = ticks::to_price(best_ask_);
            b.ask_qty = ask_levels_.at(best_ask_);
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        return top_levels(bid_levels_, depth, /*descending=*/true);
    }

    std::vector<Level> get_asks(int depth = 10) const {
        return top_levels(ask_levels_, depth, /*descending=*/false);
    }

private:
    struct OrderRec {
        Side side;
        int tick;
        long qty;
    };
    static constexpr int kNone = -1;

    using LevelMap = std::unordered_map<int, long>;

    // Returns true if the level was fully removed.
    static bool drain(LevelMap& m, int tick, long qty) {
        auto it = m.find(tick);
        if (it == m.end()) return false;
        it->second -= qty;
        if (it->second <= 0) {
            m.erase(it);
            return true;
        }
        return false;
    }

    static int recompute_max(const LevelMap& m) {
        int best = kNone;
        for (const auto& kv : m)
            if (kv.first > best) best = kv.first;
        return best;
    }

    static int recompute_min(const LevelMap& m) {
        int best = kNone;
        for (const auto& kv : m)
            if (best == kNone || kv.first < best) best = kv.first;
        return best;
    }

    static std::vector<Level> top_levels(const LevelMap& m, int depth, bool descending) {
        std::vector<std::pair<int, long>> all(m.begin(), m.end());
        const int n = std::min<int>(depth, (int)all.size());
        if (descending)
            std::partial_sort(all.begin(), all.begin() + n, all.end(),
                              [](const auto& a, const auto& b) { return a.first > b.first; });
        else
            std::partial_sort(all.begin(), all.begin() + n, all.end(),
                              [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<Level> out;
        out.reserve(n);
        for (int i = 0; i < n; ++i)
            out.push_back({ticks::to_price(all[i].first), all[i].second});
        return out;
    }

    LevelMap bid_levels_;
    LevelMap ask_levels_;
    std::unordered_map<uint64_t, OrderRec> orders_;
    int best_bid_ = kNone;
    int best_ask_ = kNone;
};

#endif  // CAPSTONE_IMPL_B_HASHMAP_H
