// =============================================================================
// vector_order_book.cpp  –  Implementation B
// =============================================================================
// Sorted vector approach:
//   bid_levels_  sorted DESCENDING (highest price first, index 0 = best bid)
//   ask_levels_  sorted ASCENDING  (lowest  price first, index 0 = best ask)
//
// Parallel integer tick vectors (bid_ticks_, ask_ticks_) are kept alongside
// the Level vectors so binary search can use integer comparison rather than
// floating-point comparison.
//
// Inserting or erasing a price level requires shifting all elements to the
// right of that position — O(L) time where L is the number of active levels.
// This cost is real and is NOT claimed to be O(1).
// =============================================================================
#include "vector_order_book.hpp"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace hft {

// ---------------------------------------------------------------------------
// find_bid_index / find_ask_index
// ---------------------------------------------------------------------------
// Bids are stored DESCENDING, so lower_bound must use std::greater<int> to
// walk the vector in the correct direction.  Returns size() if not found.

std::size_t VectorOrderBook::find_bid_index(int tick) const
{
    auto it = std::lower_bound(bid_ticks_.begin(), bid_ticks_.end(), tick,
                               std::greater<int>{});
    if (it == bid_ticks_.end() || *it != tick) return bid_ticks_.size();
    return static_cast<std::size_t>(it - bid_ticks_.begin());
}

std::size_t VectorOrderBook::find_ask_index(int tick) const
{
    auto it = std::lower_bound(ask_ticks_.begin(), ask_ticks_.end(), tick);
    if (it == ask_ticks_.end() || *it != tick) return ask_ticks_.size();
    return static_cast<std::size_t>(it - ask_ticks_.begin());
}

// ---------------------------------------------------------------------------

void VectorOrderBook::add_order(std::uint64_t id, Side side, double price, long qty)
{
    if (qty <= 0) {
        throw std::invalid_argument("add_order: qty must be > 0");
    }
    // price_to_tick throws for NaN, Inf, out-of-range, and off-tick prices.
    int tick = price_to_tick(price);
    if (!is_valid_tick_for_side(tick, side)) {
        throw std::invalid_argument(
            "add_order: price " + std::to_string(price) +
            " is not valid for the given side");
    }
    if (order_map_.count(id)) {
        throw std::invalid_argument(
            "add_order: duplicate order id " + std::to_string(id));
    }

    // Exception-safety ordering:
    //   1. Check overflow (no side effects).
    //   2. Insert into order_map_ (may throw std::bad_alloc).
    //   3a. If the level already exists: increment qty (no allocation possible).
    //   3b. If new level: insert the tick and level entries.
    //       Both vector inserts may allocate.  We insert the tick first, then
    //       the level.  If the level insert throws, we roll back the tick insert
    //       so the two parallel vectors stay in sync.
    //   If step 3 throws in any case, we roll back the order_map_ entry too.

    if (side == Side::Buy) {
        std::size_t idx = find_bid_index(tick);

        // Step 1: overflow check.
        if (idx < bid_ticks_.size() &&
            bid_levels_[idx].qty > std::numeric_limits<long>::max() - qty) {
            throw std::overflow_error("add_order: quantity overflow at bid level");
        }

        // Step 2: record order.
        order_map_[id] = {side, tick, qty};

        // Step 3: mutate price-level storage with rollback.
        try {
            if (idx < bid_ticks_.size()) {
                // Existing level: simple increment, no allocation.
                bid_levels_[idx].qty += qty;
            } else {
                auto pos = std::lower_bound(bid_ticks_.begin(), bid_ticks_.end(), tick,
                                            std::greater<int>{});
                std::size_t insert_at = static_cast<std::size_t>(pos - bid_ticks_.begin());

                // Insert tick first.
                bid_ticks_.insert(bid_ticks_.begin() + static_cast<std::ptrdiff_t>(insert_at), tick);
                // Insert level; if this throws, roll back the tick insert.
                try {
                    bid_levels_.insert(bid_levels_.begin() + static_cast<std::ptrdiff_t>(insert_at),
                                       Level{tick_to_price(tick), qty});
                } catch (...) {
                    bid_ticks_.erase(bid_ticks_.begin() + static_cast<std::ptrdiff_t>(insert_at));
                    throw;
                }
            }
        } catch (...) {
            order_map_.erase(id);
            throw;
        }
    } else {
        std::size_t idx = find_ask_index(tick);

        if (idx < ask_ticks_.size() &&
            ask_levels_[idx].qty > std::numeric_limits<long>::max() - qty) {
            throw std::overflow_error("add_order: quantity overflow at ask level");
        }

        order_map_[id] = {side, tick, qty};

        try {
            if (idx < ask_ticks_.size()) {
                ask_levels_[idx].qty += qty;
            } else {
                auto pos = std::lower_bound(ask_ticks_.begin(), ask_ticks_.end(), tick);
                std::size_t insert_at = static_cast<std::size_t>(pos - ask_ticks_.begin());

                ask_ticks_.insert(ask_ticks_.begin() + static_cast<std::ptrdiff_t>(insert_at), tick);
                try {
                    ask_levels_.insert(ask_levels_.begin() + static_cast<std::ptrdiff_t>(insert_at),
                                       Level{tick_to_price(tick), qty});
                } catch (...) {
                    ask_ticks_.erase(ask_ticks_.begin() + static_cast<std::ptrdiff_t>(insert_at));
                    throw;
                }
            }
        } catch (...) {
            order_map_.erase(id);
            throw;
        }
    }
}

bool VectorOrderBook::cancel_order(std::uint64_t id)
{
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;

    const OrderMeta& meta = it->second;

    if (meta.side == Side::Buy) {
        std::size_t idx = find_bid_index(meta.tick);
        bid_levels_[idx].qty -= meta.qty;
        if (bid_levels_[idx].qty == 0) {
            // Erasing an empty level shifts all subsequent elements — O(L).
            bid_levels_.erase(bid_levels_.begin() + static_cast<std::ptrdiff_t>(idx));
            bid_ticks_.erase(bid_ticks_.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    } else {
        std::size_t idx = find_ask_index(meta.tick);
        ask_levels_[idx].qty -= meta.qty;
        if (ask_levels_[idx].qty == 0) {
            ask_levels_.erase(ask_levels_.begin() + static_cast<std::ptrdiff_t>(idx));
            ask_ticks_.erase(ask_ticks_.begin() + static_cast<std::ptrdiff_t>(idx));
        }
    }

    order_map_.erase(it);
    return true;
}

BBO VectorOrderBook::get_bbo() const
{
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (!bid_levels_.empty()) {
        bbo.has_bid   = true;
        bbo.bid_price = bid_levels_.front().price;
        bbo.bid_qty   = bid_levels_.front().qty;
    }
    if (!ask_levels_.empty()) {
        bbo.has_ask   = true;
        bbo.ask_price = ask_levels_.front().price;
        bbo.ask_qty   = ask_levels_.front().qty;
    }
    return bbo;
}

std::vector<Level> VectorOrderBook::get_bids(int depth) const
{
    if (depth <= 0) return {};
    std::size_t n = std::min(static_cast<std::size_t>(depth), bid_levels_.size());
    return std::vector<Level>(bid_levels_.begin(), bid_levels_.begin() + static_cast<std::ptrdiff_t>(n));
}

std::vector<Level> VectorOrderBook::get_asks(int depth) const
{
    if (depth <= 0) return {};
    std::size_t n = std::min(static_cast<std::size_t>(depth), ask_levels_.size());
    return std::vector<Level>(ask_levels_.begin(), ask_levels_.begin() + static_cast<std::ptrdiff_t>(n));
}

} // namespace hft
