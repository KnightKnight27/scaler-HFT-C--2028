
#if !defined(__GNUC__) && !defined(__clang__)
#define __builtin_expect(expr, val) (expr)
#define __builtin_ctzll(x) 0
#endif

#include "orderbook_c.hpp"

OrderBookC::OrderBookC()
    : best_bid_idx_(INVALID_IDX),
      best_ask_idx_(INVALID_IDX),
      order_pool_(MAX_DIRECT_ID) {
    bids_.fill(0);
    asks_.fill(0);
    bid_bits_.fill(0ULL);
    ask_bits_.fill(0ULL);
}

void OrderBookC::add_order(uint64_t id, Side side, double price, long qty) {
    if (qty <= 0) return;
    const int32_t tick = price_to_tick(price);

    if (side == Side::Buy) {
        const int32_t idx = MAX_BID_TICK - tick; // 9999 - tick
        if (idx < 0 || idx >= NUM_LEVELS_PER_SIDE) return;

        if (bids_[idx] == 0) {
            set_bit(bid_bits_, idx);
            if (idx < best_bid_idx_) {
                best_bid_idx_ = idx;
            }
        }
        bids_[idx] += qty;

        if (__builtin_expect(id < MAX_DIRECT_ID, 1)) {
            order_pool_[id] = OrderSlot{qty, static_cast<uint16_t>(idx), 0, 1};
        } else {
            overflow_orders_[id] = OrderSlot{qty, static_cast<uint16_t>(idx), 0, 1};
        }
    } else {
        const int32_t idx = tick - MIN_ASK_TICK; // tick - 10001
        if (idx < 0 || idx >= NUM_LEVELS_PER_SIDE) return;

        if (asks_[idx] == 0) {
            set_bit(ask_bits_, idx);
            if (idx < best_ask_idx_) {
                best_ask_idx_ = idx;
            }
        }
        asks_[idx] += qty;

        if (__builtin_expect(id < MAX_DIRECT_ID, 1)) {
            order_pool_[id] = OrderSlot{qty, static_cast<uint16_t>(idx), 1, 1};
        } else {
            overflow_orders_[id] = OrderSlot{qty, static_cast<uint16_t>(idx), 1, 1};
        }
    }
}

bool OrderBookC::cancel_order(uint64_t id) {
    OrderSlot slot{};
    if (__builtin_expect(id < MAX_DIRECT_ID, 1)) {
        if (!order_pool_[id].active) {
            return false;
        }
        slot = order_pool_[id];
        order_pool_[id].active = 0;
    } else {
        auto it = overflow_orders_.find(id);
        if (it == overflow_orders_.end() || !it->second.active) {
            return false;
        }
        slot = it->second;
        overflow_orders_.erase(it);
    }

    const int32_t idx = slot.idx;
    if (slot.side == 0) {
        bids_[idx] -= slot.qty;
        if (bids_[idx] <= 0) {
            bids_[idx] = 0;
            clear_bit(bid_bits_, idx);
            if (idx == best_bid_idx_) {
                best_bid_idx_ = find_next_set_bit(bid_bits_, idx + 1);
            }
        }
    } else {
        asks_[idx] -= slot.qty;
        if (asks_[idx] <= 0) {
            asks_[idx] = 0;
            clear_bit(ask_bits_, idx);
            if (idx == best_ask_idx_) {
                best_ask_idx_ = find_next_set_bit(ask_bits_, idx + 1);
            }
        }
    }
    return true;
}

BBO OrderBookC::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (best_bid_idx_ < INVALID_IDX) {
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(MAX_BID_TICK - best_bid_idx_);
        bbo.bid_qty = bids_[best_bid_idx_];
    }
    if (best_ask_idx_ < INVALID_IDX) {
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(MIN_ASK_TICK + best_ask_idx_);
        bbo.ask_qty = asks_[best_ask_idx_];
    }
    return bbo;
}

std::vector<Level> OrderBookC::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_bid_idx_ >= INVALID_IDX) return result;
    result.reserve(static_cast<size_t>(depth));

    int32_t idx = best_bid_idx_;
    int count = 0;
    while (idx < INVALID_IDX && count < depth) {
        result.push_back(Level{tick_to_price(MAX_BID_TICK - idx), bids_[idx]});
        ++count;
        if (count == depth) break;
        idx = find_next_set_bit(bid_bits_, idx + 1);
    }
    return result;
}

std::vector<Level> OrderBookC::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_ask_idx_ >= INVALID_IDX) return result;
    result.reserve(static_cast<size_t>(depth));

    int32_t idx = best_ask_idx_;
    int count = 0;
    while (idx < INVALID_IDX && count < depth) {
        result.push_back(Level{tick_to_price(MIN_ASK_TICK + idx), asks_[idx]});
        ++count;
        if (count == depth) break;
        idx = find_next_set_bit(ask_bits_, idx + 1);
    }
    return result;
}