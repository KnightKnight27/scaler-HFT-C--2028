#pragma once
// =============================================================================
// Implementation C: Fixed-Size Tick-Indexed Array (Custom Design)
// =============================================================================
// Design:
//   This is my own design. Instead of a tree or a sorted list, we use a flat
//   array of 10 001 slots — one slot per possible tick.  Since the price range
//   is fixed and bounded, we can directly index into the array in O(1).
//
//   Key ideas:
//
//   1. Flat level array (levels_[]):
//      levels_[tick - TICK_MIN].qty  holds the aggregate quantity at that tick.
//      Zero qty means no orders rest at that tick.
//      Array is statically sized → no heap allocation, fits in cache.
//
//   2. Best-bid / best-ask cursors (best_bid_, best_ask_):
//      Maintained lazily:
//        - On add_order we may update the best cursor upward (bid) or
//          downward (ask) in O(1).
//        - On cancel_order we may need to scan inward to find the new best,
//          but only when the cancelled level was the best and becomes empty.
//          Worst-case scan is O(N) but amortised O(1) in typical workloads
//          because prices cluster near the mid.
//      Because BBO is almost always cached, get_bbo() is O(1).
//
//   3. Per-order map (orders_):
//      std::unordered_map<uint64_t, Order> for cancellation lookup.
//      The value stores only the tick and qty, so cancellation is O(1) lookup
//      + O(1) array update + amortised-O(1) cursor repair.
//
// Complexity:
//   add_order    : O(1)         — direct slot update + cursor compare
//   cancel_order : O(1) amort.  — O(1) lookup + O(1) slot update + O(N) worst-
//                                  case cursor rescan (only when best level empties)
//   get_bbo      : O(1)         — read cached cursors
//   get_bids     : O(D)         — walk from best_bid downward, max D steps
//   get_asks     : O(D)         — walk from best_ask upward, max D steps
//
// Memory:
//   ~10 001 * 8 bytes (long) = ~80 KB for the level array → fits in L2 cache.
// =============================================================================

#include "common.h"
#include <array>
#include <unordered_map>
#include <vector>
#include <cstdint>

class OrderBookC {
public:
    OrderBookC() {
        levels_.fill(0);
        best_bid_ = -1;       // sentinel: no bid
        best_ask_ = INT_MAX;  // sentinel: no ask
    }

    // --- Mutators -----------------------------------------------------------

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = Order{id, side, tick, qty};

        int slot = tick - TICK_MIN;
        levels_[slot] += qty;

        if (side == Side::Buy) {
            if (tick > best_bid_) best_bid_ = tick;
        } else {
            if (tick < best_ask_) best_ask_ = tick;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        const Order& o = it->second;
        int slot = o.tick - TICK_MIN;
        levels_[slot] -= o.qty;

        if (levels_[slot] == 0) {
            // The level emptied — repair cursors if needed
            if (o.side == Side::Buy && o.tick == best_bid_) {
                repair_best_bid();
            } else if (o.side == Side::Sell && o.tick == best_ask_) {
                repair_best_ask();
            }
        }

        orders_.erase(it);
        return true;
    }

    // --- Queries ------------------------------------------------------------

    BBO get_bbo() const {
        BBO bbo;
        if (best_bid_ >= BID_TICK_MIN) {
            bbo.has_bid   = true;
            bbo.bid_price = tick_to_price(best_bid_);
            bbo.bid_qty   = levels_[best_bid_ - TICK_MIN];
        }
        if (best_ask_ <= ASK_TICK_MAX) {
            bbo.has_ask   = true;
            bbo.ask_price = tick_to_price(best_ask_);
            bbo.ask_qty   = levels_[best_ask_ - TICK_MIN];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (best_bid_ < BID_TICK_MIN) return result;  // empty book guard
        result.reserve(static_cast<size_t>(depth));
        int count = 0;
        for (int t = best_bid_; t >= BID_TICK_MIN && count < depth; --t) {
            long q = levels_[t - TICK_MIN];
            if (q > 0) {
                result.push_back({tick_to_price(t), q});
                ++count;
            }
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (best_ask_ > ASK_TICK_MAX) return result;  // empty book guard
        result.reserve(static_cast<size_t>(depth));
        int count = 0;
        for (int t = best_ask_; t <= ASK_TICK_MAX && count < depth; ++t) {
            long q = levels_[t - TICK_MIN];
            if (q > 0) {
                result.push_back({tick_to_price(t), q});
                ++count;
            }
        }
        return result;
    }

private:
    // ---- internal helpers --------------------------------------------------

    void repair_best_bid() {
        // Walk downward from best_bid_ to find the next non-empty bid level.
        for (int t = best_bid_ - 1; t >= BID_TICK_MIN; --t) {
            if (levels_[t - TICK_MIN] > 0) {
                best_bid_ = t;
                return;
            }
        }
        best_bid_ = -1; // book is empty on the bid side
    }

    void repair_best_ask() {
        for (int t = best_ask_ + 1; t <= ASK_TICK_MAX; ++t) {
            if (levels_[t - TICK_MIN] > 0) {
                best_ask_ = t;
                return;
            }
        }
        best_ask_ = INT_MAX; // book is empty on the ask side
    }

    // ---- data members ------------------------------------------------------

    // Flat tick-indexed quantity array (statically allocated)
    std::array<long, NUM_SLOTS> levels_;

    // Cached best prices (integer ticks)
    int best_bid_;   // -1 means no bid
    int best_ask_;   // INT_MAX means no ask

    // Per-order metadata for cancellation
    std::unordered_map<uint64_t, Order> orders_;
};
