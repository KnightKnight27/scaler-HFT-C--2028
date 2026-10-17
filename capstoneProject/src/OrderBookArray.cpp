#include "OrderBookArray.hpp"
#include <algorithm>

const int MIN_TICK = 5000;
const int NUM_TICKS = 10001;

int OrderBookArray::tick_to_index(PriceTick tick) const {
    int idx = tick - MIN_TICK;
    if (idx < 0) idx = 0;
    if (idx >= NUM_TICKS) idx = NUM_TICKS - 1;
    return idx;
}

PriceTick OrderBookArray::index_to_tick(int idx) const {
    return MIN_TICK + idx;
}

OrderBookArray::OrderBookArray() {
    best_bid_idx_ = -1;
    best_ask_idx_ = NUM_TICKS;
    level_qty_.fill(0);
}

void OrderBookArray::add_order(uint64_t id, Side side, double price, long qty) {
    PriceTick tick = price_to_ticks(price);
    int idx = tick_to_index(tick);
    orders_[id] = {side, idx, qty};

    level_qty_[idx] += qty;

    if (side == Side::Buy) {
        if (best_bid_idx_ == -1 || idx > best_bid_idx_) {
            best_bid_idx_ = idx;
        }
    } else {
        if (best_ask_idx_ == NUM_TICKS || idx < best_ask_idx_) {
            best_ask_idx_ = idx;
        }
    }
}

bool OrderBookArray::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }

    const auto& rec = it->second;
    int idx = rec.idx;
    level_qty_[idx] -= rec.qty;

    if (level_qty_[idx] <= 0) {
        level_qty_[idx] = 0;
        if (rec.side == Side::Buy) {
            if (idx == best_bid_idx_) {
                while (best_bid_idx_ >= 0 && level_qty_[best_bid_idx_] == 0) {
                    --best_bid_idx_;
                }
            }
        } else {
            if (idx == best_ask_idx_) {
                while (best_ask_idx_ < NUM_TICKS && level_qty_[best_ask_idx_] == 0) {
                    ++best_ask_idx_;
                }
            }
        }
    }

    orders_.erase(it);
    return true;
}

BBO OrderBookArray::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (best_bid_idx_ >= 0) {
        bbo.has_bid = true;
        bbo.bid_price = ticks_to_price(index_to_tick(best_bid_idx_));
        bbo.bid_qty = level_qty_[best_bid_idx_];
    }
    if (best_ask_idx_ < NUM_TICKS) {
        bbo.has_ask = true;
        bbo.ask_price = ticks_to_price(index_to_tick(best_ask_idx_));
        bbo.ask_qty = level_qty_[best_ask_idx_];
    }
    return bbo;
}

std::vector<Level> OrderBookArray::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_bid_idx_ < 0) return result;
    result.reserve(depth);
    for (int i = best_bid_idx_; i >= 0 && (int)result.size() < depth; --i) {
        if (level_qty_[i] > 0) {
            result.push_back({ticks_to_price(index_to_tick(i)), level_qty_[i]});
        }
    }
    return result;
}

std::vector<Level> OrderBookArray::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_ask_idx_ >= NUM_TICKS) return result;
    result.reserve(depth);
    for (int i = best_ask_idx_; i < NUM_TICKS && (int)result.size() < depth; ++i) {
        if (level_qty_[i] > 0) {
            result.push_back({ticks_to_price(index_to_tick(i)), level_qty_[i]});
        }
    }
    return result;
}
