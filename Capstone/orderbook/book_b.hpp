// Implementation B: hash map for levels + linear scan for best price.
//
// This is deliberately the "no ordering" design, opposite of A:
//   - bids/asks: unordered_map<tick, total qty>. Add and cancel are plain
//     hash table ops, no tree rebalancing, no pointer chasing between nodes.
//   - The price: we keep NO best pointer and NO sorted structure. Every
//     get_bbo / get_bids / get_asks scans every live level to find the
//     best one(s). Simple but gets slow as the book fills up.
//
// Complexity (M = live price levels):
//   add_order    O(1) average (hash insert)
//   cancel_order O(1) average (hash lookup + update)
//   get_bbo      O(M)  (must scan all levels to find min/max)
//   get_bids/asks O(M * depth) (depth times: scan for next best level;
//                  depth is small (10) so this is ~linear in practice)

#pragma once

#include "common.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <limits>

class HashScanOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = {side, tick, qty};
        if (side == Side::Buy)
            bids_[tick] += qty;
        else
            asks_[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end())
            return false;

        OrderInfo o = it->second;
        orders_.erase(it);

        std::unordered_map<int, long> &levels = (o.side == Side::Buy) ? bids_ : asks_;
        auto lv = levels.find(o.tick);
        if (lv != levels.end()) {
            lv->second -= o.qty;
            if (lv->second <= 0)
                levels.erase(lv);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        int best_bid = -1;
        for (const auto &p : bids_) {
            if (p.second > 0 && p.first > best_bid) {
                best_bid = p.first;
                b.bid_qty = p.second;
            }
        }
        if (best_bid != -1) {
            b.has_bid = true;
            b.bid_price = tick_to_price(best_bid);
        }
        int best_ask = std::numeric_limits<int>::max();
        for (const auto &p : asks_) {
            if (p.second > 0 && p.first < best_ask) {
                best_ask = p.first;
                b.ask_qty = p.second;
            }
        }
        if (best_ask != std::numeric_limits<int>::max()) {
            b.has_ask = true;
            b.ask_price = tick_to_price(best_ask);
        }
        return b;
    }

    // Top `depth` levels: repeatedly scan for the next best level,
    // skipping ticks we already picked. O(M * depth).
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        int below = std::numeric_limits<int>::max(); // only take ticks < this
        for (int d = 0; d < depth; ++d) {
            int best = -1;
            long qty = 0;
            for (const auto &p : bids_) {
                if (p.second > 0 && p.first < below && p.first > best) {
                    best = p.first;
                    qty = p.second;
                }
            }
            if (best == -1)
                break;
            out.push_back({tick_to_price(best), qty});
            below = best;
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        int above = -1; // only take ticks > this
        for (int d = 0; d < depth; ++d) {
            int best = std::numeric_limits<int>::max();
            long qty = 0;
            for (const auto &p : asks_) {
                if (p.second > 0 && p.first > above && p.first < best) {
                    best = p.first;
                    qty = p.second;
                }
            }
            if (best == std::numeric_limits<int>::max())
                break;
            out.push_back({tick_to_price(best), qty});
            above = best;
        }
        return out;
    }

private:
    struct OrderInfo {
        Side side;
        int tick;
        long qty;
    };

    std::unordered_map<int, long> bids_; // tick -> total qty (unsorted)
    std::unordered_map<int, long> asks_; // tick -> total qty (unsorted)
    std::unordered_map<uint64_t, OrderInfo> orders_; // id -> order
};
