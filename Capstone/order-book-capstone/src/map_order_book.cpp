// =============================================================================
// map_order_book.cpp  –  Implementation A
// =============================================================================
#include "map_order_book.hpp"
#include <stdexcept>
#include <limits>

namespace hft {

void MapOrderBook::add_order(std::uint64_t id, Side side, double price, long qty)
{
    if (qty <= 0) {
        throw std::invalid_argument("add_order: qty must be > 0");
    }

    // price_to_tick throws std::invalid_argument for NaN, Inf, out-of-range,
    // and off-tick prices; we then check that the tick is valid for this side.
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
    //   1. Check overflow before touching any state.
    //   2. Insert into order_map_ (may throw std::bad_alloc).
    //   3. Mutate the price-level map — map::operator[] and map::find+increment
    //      do not throw once the overflow check has passed, so the book
    //      stays consistent.
    //
    // If step 2 throws, no state has been mutated.
    // Step 3 uses find-then-increment (existing level) or operator[] (new level).
    // operator[] on std::map may allocate; but if it throws here the order_map_
    // entry is already committed — we must roll it back.

    // Step 1: overflow check (no side effects).
    if (side == Side::Buy) {
        auto it = bids_.find(tick);
        if (it != bids_.end() && it->second > std::numeric_limits<long>::max() - qty) {
            throw std::overflow_error("add_order: quantity overflow at bid level");
        }
    } else {
        auto it = asks_.find(tick);
        if (it != asks_.end() && it->second > std::numeric_limits<long>::max() - qty) {
            throw std::overflow_error("add_order: quantity overflow at ask level");
        }
    }

    // Step 2: record the order (may throw std::bad_alloc).
    order_map_[id] = {side, tick, qty};

    // Step 3: mutate price-level storage. Roll back order_map_ if this throws.
    try {
        if (side == Side::Buy) {
            auto it = bids_.find(tick);
            if (it != bids_.end()) {
                it->second += qty;
            } else {
                bids_[tick] = qty;   // may throw std::bad_alloc
            }
        } else {
            auto it = asks_.find(tick);
            if (it != asks_.end()) {
                it->second += qty;
            } else {
                asks_[tick] = qty;
            }
        }
    } catch (...) {
        order_map_.erase(id);
        throw;
    }
}

bool MapOrderBook::cancel_order(std::uint64_t id)
{
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;

    const OrderMeta& meta = it->second;

    if (meta.side == Side::Buy) {
        auto bit = bids_.find(meta.tick);
        bit->second -= meta.qty;
        if (bit->second == 0) bids_.erase(bit);
    } else {
        auto ait = asks_.find(meta.tick);
        ait->second -= meta.qty;
        if (ait->second == 0) asks_.erase(ait);
    }

    order_map_.erase(it);
    return true;
}

BBO MapOrderBook::get_bbo() const
{
    BBO bbo{false, 0.0, 0, false, 0.0, 0};

    // bids_ uses std::greater, so begin() is always the highest (best) price.
    // asks_ uses the default less, so begin() is always the lowest (best) price.
    if (!bids_.empty()) {
        bbo.has_bid   = true;
        bbo.bid_price = tick_to_price(bids_.begin()->first);
        bbo.bid_qty   = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        bbo.has_ask   = true;
        bbo.ask_price = tick_to_price(asks_.begin()->first);
        bbo.ask_qty   = asks_.begin()->second;
    }
    return bbo;
}

std::vector<Level> MapOrderBook::get_bids(int depth) const
{
    if (depth <= 0) return {};
    std::vector<Level> result;
    result.reserve(static_cast<std::size_t>(depth));
    int count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < depth; ++it, ++count) {
        result.push_back({tick_to_price(it->first), it->second});
    }
    return result;
}

std::vector<Level> MapOrderBook::get_asks(int depth) const
{
    if (depth <= 0) return {};
    std::vector<Level> result;
    result.reserve(static_cast<std::size_t>(depth));
    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
        result.push_back({tick_to_price(it->first), it->second});
    }
    return result;
}

} // namespace hft
