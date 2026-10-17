// =============================================================================
// array_order_book.cpp  –  Implementation C
// =============================================================================
#include "array_order_book.hpp"
#include <stdexcept>
#include <limits>

namespace hft {

ArrayOrderBook::ArrayOrderBook()
    : best_bid_tick_(-1),
      best_ask_tick_(std::numeric_limits<int>::max())
{
    for (int i = 0; i < BID_SLOTS; ++i) bid_slots_[i] = {0, 0};
    for (int i = 0; i < ASK_SLOTS; ++i) ask_slots_[i] = {0, 0};
}

void ArrayOrderBook::add_order(std::uint64_t id, Side side, double price, long qty)
{
    if (qty <= 0) {
        throw std::invalid_argument("add_order: qty must be > 0");
    }
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
    //   1. Overflow check (no side effects).
    //   2. Insert into order_map_ (may throw std::bad_alloc).
    //   3. Write to the fixed-size slot array (cannot throw).
    // If step 2 throws, the array is untouched.
    // The try/catch in step 3 is defensive: array writes do not allocate, so
    // the catch branch is unreachable in practice but keeps the contract clear.

    if (side == Side::Buy) {
        Slot& s = bid_slots_[bid_index(tick)];
        if (s.qty > std::numeric_limits<long>::max() - qty) {
            throw std::overflow_error("add_order: quantity overflow at bid level");
        }
        order_map_[id] = {side, tick, qty};
        try {
            s.qty += qty;
            s.order_count++;
            if (best_bid_tick_ < tick) best_bid_tick_ = tick;
        } catch (...) {
            order_map_.erase(id);
            throw;
        }
    } else {
        Slot& s = ask_slots_[ask_index(tick)];
        if (s.qty > std::numeric_limits<long>::max() - qty) {
            throw std::overflow_error("add_order: quantity overflow at ask level");
        }
        order_map_[id] = {side, tick, qty};
        try {
            s.qty += qty;
            s.order_count++;
            if (best_ask_tick_ > tick) best_ask_tick_ = tick;
        } catch (...) {
            order_map_.erase(id);
            throw;
        }
    }
}

bool ArrayOrderBook::cancel_order(std::uint64_t id)
{
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;

    const OrderMeta& meta = it->second;

    if (meta.side == Side::Buy) {
        Slot& s = bid_slots_[bid_index(meta.tick)];
        s.qty -= meta.qty;
        s.order_count--;
        // Only rescan for a new best when the level that just emptied was
        // the current best bid; any other cancellation cannot change the best.
        if (s.order_count == 0 && meta.tick == best_bid_tick_) {
            rescan_best_bid();
        }
    } else {
        Slot& s = ask_slots_[ask_index(meta.tick)];
        s.qty -= meta.qty;
        s.order_count--;
        if (s.order_count == 0 && meta.tick == best_ask_tick_) {
            rescan_best_ask();
        }
    }

    order_map_.erase(it);
    return true;
}

// Walk downward from the old best until a non-empty slot is found.
// Sentinel -1 signals that no bids remain in the book.
void ArrayOrderBook::rescan_best_bid() noexcept
{
    int t = best_bid_tick_ - 1;
    while (t >= TICK_BID_MIN) {
        if (bid_slots_[bid_index(t)].order_count > 0) {
            best_bid_tick_ = t;
            return;
        }
        --t;
    }
    best_bid_tick_ = -1;
}

void ArrayOrderBook::rescan_best_ask() noexcept
{
    int t = best_ask_tick_ + 1;
    while (t <= TICK_ASK_MAX) {
        if (ask_slots_[ask_index(t)].order_count > 0) {
            best_ask_tick_ = t;
            return;
        }
        ++t;
    }
    best_ask_tick_ = std::numeric_limits<int>::max();
}

BBO ArrayOrderBook::get_bbo() const
{
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (best_bid_tick_ >= TICK_BID_MIN) {
        bbo.has_bid   = true;
        bbo.bid_price = tick_to_price(best_bid_tick_);
        bbo.bid_qty   = bid_slots_[bid_index(best_bid_tick_)].qty;
    }
    if (best_ask_tick_ <= TICK_ASK_MAX) {
        bbo.has_ask   = true;
        bbo.ask_price = tick_to_price(best_ask_tick_);
        bbo.ask_qty   = ask_slots_[ask_index(best_ask_tick_)].qty;
    }
    return bbo;
}

std::vector<Level> ArrayOrderBook::get_bids(int depth) const
{
    if (depth <= 0 || best_bid_tick_ < TICK_BID_MIN) return {};
    std::vector<Level> result;
    result.reserve(static_cast<std::size_t>(depth));
    int t = best_bid_tick_;
    while (t >= TICK_BID_MIN && static_cast<int>(result.size()) < depth) {
        const Slot& s = bid_slots_[bid_index(t)];
        if (s.order_count > 0) {
            result.push_back({tick_to_price(t), s.qty});
        }
        --t;
    }
    return result;
}

std::vector<Level> ArrayOrderBook::get_asks(int depth) const
{
    if (depth <= 0 || best_ask_tick_ > TICK_ASK_MAX) return {};
    std::vector<Level> result;
    result.reserve(static_cast<std::size_t>(depth));
    int t = best_ask_tick_;
    while (t <= TICK_ASK_MAX && static_cast<int>(result.size()) < depth) {
        const Slot& s = ask_slots_[ask_index(t)];
        if (s.order_count > 0) {
            result.push_back({tick_to_price(t), s.qty});
        }
        ++t;
    }
    return result;
}

} // namespace hft
