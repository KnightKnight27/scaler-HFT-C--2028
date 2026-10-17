#include "orderbook_b.hpp"
#include <algorithm>

OrderBookB::OrderBookB() : order_table_(HASH_CAPACITY) {
    bids_.reserve(NUM_LEVELS_PER_SIDE);
    asks_.reserve(NUM_LEVELS_PER_SIDE);
}

void OrderBookB::insert_order_slot(uint64_t id, Side side, int32_t tick, long qty) noexcept {
    size_t idx = hash_id(id);
    size_t first_tombstone = HASH_CAPACITY;

    while (order_table_[idx].state != SlotState::Empty) {
        if (order_table_[idx].state == SlotState::Occupied && order_table_[idx].id == id) {
            order_table_[idx].side = side;
            order_table_[idx].tick = tick;
            order_table_[idx].qty = qty;
            return;
        }
        if (order_table_[idx].state == SlotState::Tombstone && first_tombstone == HASH_CAPACITY) {
            first_tombstone = idx;
        }
        idx = (idx + 1) & HASH_MASK;
    }

    size_t target = (first_tombstone != HASH_CAPACITY) ? first_tombstone : idx;
    order_table_[target].id = id;
    order_table_[target].side = side;
    order_table_[target].tick = tick;
    order_table_[target].qty = qty;
    order_table_[target].state = SlotState::Occupied;
}

OrderBookB::HashSlot* OrderBookB::find_order_slot(uint64_t id) noexcept {
    size_t idx = hash_id(id);
    while (order_table_[idx].state != SlotState::Empty) {
        if (order_table_[idx].state == SlotState::Occupied && order_table_[idx].id == id) {
            return &order_table_[idx];
        }
        idx = (idx + 1) & HASH_MASK;
    }
    return nullptr;
}

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty) {
    if (qty <= 0) return;
    const int32_t tick = price_to_tick(price);
    insert_order_slot(id, side, tick, qty);

    if (side == Side::Buy) {
        // Ascending sort: worst bid (5000) at front, best bid (9999) at back
        auto it = std::lower_bound(bids_.begin(), bids_.end(), tick,
                                   [](const TickLevel& lvl, int32_t t) { return lvl.tick < t; });
        if (it != bids_.end() && it->tick == tick) {
            it->qty += qty;
        } else {
            bids_.insert(it, TickLevel{tick, qty});
        }
    } else {
        // Descending sort: worst ask (15000) at front, best ask (10001) at back
        auto it = std::lower_bound(asks_.begin(), asks_.end(), tick,
                                   [](const TickLevel& lvl, int32_t t) { return lvl.tick > t; });
        if (it != asks_.end() && it->tick == tick) {
            it->qty += qty;
        } else {
            asks_.insert(it, TickLevel{tick, qty});
        }
    }
}

bool OrderBookB::cancel_order(uint64_t id) {
    HashSlot* slot = find_order_slot(id);
    if (!slot) {
        return false;
    }

    const Side side = slot->side;
    const int32_t tick = slot->tick;
    const long qty = slot->qty;
    slot->state = SlotState::Tombstone;

    if (side == Side::Buy) {
        auto it = std::lower_bound(bids_.begin(), bids_.end(), tick,
                                   [](const TickLevel& lvl, int32_t t) { return lvl.tick < t; });
        if (it != bids_.end() && it->tick == tick) {
            it->qty -= qty;
            if (it->qty <= 0) {
                bids_.erase(it);
            }
        }
    } else {
        auto it = std::lower_bound(asks_.begin(), asks_.end(), tick,
                                   [](const TickLevel& lvl, int32_t t) { return lvl.tick > t; });
        if (it != asks_.end() && it->tick == tick) {
            it->qty -= qty;
            if (it->qty <= 0) {
                asks_.erase(it);
            }
        }
    }
    return true;
}

BBO OrderBookB::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (!bids_.empty()) {
        const TickLevel& best_bid = bids_.back();
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(best_bid.tick);
        bbo.bid_qty = best_bid.qty;
    }
    if (!asks_.empty()) {
        const TickLevel& best_ask = asks_.back();
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(best_ask.tick);
        bbo.ask_qty = best_ask.qty;
    }
    return bbo;
}

std::vector<Level> OrderBookB::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || bids_.empty()) return result;
    const size_t count = std::min(static_cast<size_t>(depth), bids_.size());
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        const TickLevel& lvl = bids_[bids_.size() - 1 - i];
        result.push_back(Level{tick_to_price(lvl.tick), lvl.qty});
    }
    return result;
}

std::vector<Level> OrderBookB::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || asks_.empty()) return result;
    const size_t count = std::min(static_cast<size_t>(depth), asks_.size());
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        const TickLevel& lvl = asks_[asks_.size() - 1 - i];
        result.push_back(Level{tick_to_price(lvl.tick), lvl.qty});
    }
    return result;
}