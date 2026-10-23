// Implementation C: my own design -- price-indexed arrays + cached best.
//
// Idea: the price range is fixed and tiny (only 10,000 possible ticks), so
// instead of a tree or hash table I use a plain array where the index IS
// the price. Level lookup is then just array[tick], no hashing, no tree.
//
//   - bid_qty[tick] / ask_qty[tick]: total resting qty at each tick.
//     Index 0 is unused; ticks go up to 15000.
//   - orders_: unordered_map<id, OrderInfo>, same as A and B, for cancels.
//   - best_bid_ / best_ask_: cached best prices.
//       * add: if the new tick is better, update the cache (O(1)).
//       * cancel: if we just emptied the cached-best level, walk to the
//         next non-empty level. The walk is O(distance) but each level is
//         visited at most once per "fill up" cycle, so it averages out.
//         (Has to scan at most the 5000 ticks on one side in the worst case.)
//       * get_bbo: just read the cache, O(1), no search at all.
//   - get_bids/asks(depth): walk at most `depth` non-empty slots starting
//     from the best. Empty slots are skipped, so worst case is a 5000-step
//     walk but normally just a few steps.
//
// Memory: two arrays of 15001 longs = ~240KB, sits nicely in L2/L3.
// No per-level allocations ever -- a level appearing/disappearing only
// writes a number into an array slot.
//
// Complexity:
//   add_order    O(1)
//   cancel_order O(1) average (hash lookup + array update; occasional scan
//                to repair the cached best after emptying a level)
//   get_bbo      O(1)
//   get_bids/asks O(depth + empty slots skipped)

#pragma once

#include "common.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

class ArrayOrderBook {
public:
    // Tick bands from the spec: bids 5000..9999, asks 10001..15000.
    static const int MAX_TICK = 15000;
    static const int MIN_BID = 5000;
    static const int MAX_BID = 9999;
    static const int MIN_ASK = 10001;
    static const int MAX_ASK = 15000;

    ArrayOrderBook() : bid_qty_(MAX_TICK + 1, 0), ask_qty_(MAX_TICK + 1, 0) {}

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = {side, tick, qty};
        if (side == Side::Buy) {
            bid_qty_[tick] += qty;
            if (!has_bid_ || tick > best_bid_) {
                best_bid_ = tick;
                has_bid_ = true;
            }
        } else {
            ask_qty_[tick] += qty;
            if (!has_ask_ || tick < best_ask_) {
                best_ask_ = tick;
                has_ask_ = true;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end())
            return false;

        OrderInfo o = it->second;
        orders_.erase(it);

        if (o.side == Side::Buy) {
            bid_qty_[o.tick] -= o.qty;
            if (bid_qty_[o.tick] <= 0) {
                bid_qty_[o.tick] = 0;
                if (has_bid_ && o.tick == best_bid_)
                    repair_best_bid(); // emptied the best level, find next
            }
        } else {
            ask_qty_[o.tick] -= o.qty;
            if (ask_qty_[o.tick] <= 0) {
                ask_qty_[o.tick] = 0;
                if (has_ask_ && o.tick == best_ask_)
                    repair_best_ask();
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (has_bid_) {
            b.has_bid = true;
            b.bid_price = tick_to_price(best_bid_);
            b.bid_qty = bid_qty_[best_bid_];
        }
        if (has_ask_) {
            b.has_ask = true;
            b.ask_price = tick_to_price(best_ask_);
            b.ask_qty = ask_qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (!has_bid_)
            return out;
        for (int t = best_bid_; t >= MIN_BID && (int)out.size() < depth; --t) {
            if (bid_qty_[t] > 0)
                out.push_back({tick_to_price(t), bid_qty_[t]});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (!has_ask_)
            return out;
        for (int t = best_ask_; t <= MAX_ASK && (int)out.size() < depth; ++t) {
            if (ask_qty_[t] > 0)
                out.push_back({tick_to_price(t), ask_qty_[t]});
        }
        return out;
    }

private:
    struct OrderInfo {
        Side side;
        int tick;
        long qty;
    };

    // Walk down from the old best until a non-empty bid level is found.
    void repair_best_bid() {
        int t = best_bid_ - 1;
        while (t >= MIN_BID && bid_qty_[t] == 0)
            --t;
        if (t >= MIN_BID) {
            best_bid_ = t;
        } else {
            has_bid_ = false; // book has no bids left
        }
    }

    void repair_best_ask() {
        int t = best_ask_ + 1;
        while (t <= MAX_ASK && ask_qty_[t] == 0)
            ++t;
        if (t <= MAX_ASK) {
            best_ask_ = t;
        } else {
            has_ask_ = false;
        }
    }

    std::vector<long> bid_qty_; // index = tick
    std::vector<long> ask_qty_; // index = tick
    std::unordered_map<uint64_t, OrderInfo> orders_;

    int best_bid_ = 0;
    int best_ask_ = 0;
    bool has_bid_ = false;
    bool has_ask_ = false;
};
