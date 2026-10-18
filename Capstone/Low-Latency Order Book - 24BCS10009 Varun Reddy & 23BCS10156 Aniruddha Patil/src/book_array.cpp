#include "book_array.hpp"

#include <algorithm>

bool OrderBookArray::add_order(uint64_t id, Side side, double price, long qty) {
    int32_t tick;
    if (!px::validate_order(side, price, qty, tick)) return false;

    const int idx = tick - px::kMinTick;
    const bool is_buy = side == Side::Buy;
    if (!orders_.insert(OrderSlot{id, static_cast<int32_t>(qty), static_cast<uint16_t>(idx),
                                  static_cast<uint8_t>(is_buy), 1}))
        return false;  // duplicate live id

    if (level_qty_[idx] == 0) occupied_.set(idx);
    level_qty_[idx] += qty;

    if (is_buy) {
        if (best_bid_ == kNone || idx > best_bid_) best_bid_ = idx;
    } else {
        if (best_ask_ == kNone || idx < best_ask_) best_ask_ = idx;
    }
    return true;
}

bool OrderBookArray::cancel_order(uint64_t id) {
    OrderSlot o;
    if (!orders_.erase(id, o)) return false;

    const int idx = o.tick_idx;
    level_qty_[idx] -= o.qty;
    if (level_qty_[idx] == 0) {
        occupied_.clear(idx);
        // Bids live strictly below the mid and asks strictly above it, so searching
        // down from a bid (or up from an ask) can never land on the other side.
        if (idx == best_bid_)
            best_bid_ = occupied_.find_prev(idx - 1);
        else if (idx == best_ask_)
            best_ask_ = occupied_.find_next(idx + 1);
    }
    return true;
}

BBO OrderBookArray::get_bbo() const {
    BBO b{};
    if (best_bid_ != kNone) {
        b.has_bid = true;
        b.bid_price = index_price(best_bid_);
        b.bid_qty = level_qty_[best_bid_];
    }
    if (best_ask_ != kNone) {
        b.has_ask = true;
        b.ask_price = index_price(best_ask_);
        b.ask_qty = level_qty_[best_ask_];
    }
    return b;
}

std::vector<Level> OrderBookArray::get_bids(int depth) const {
    std::vector<Level> out;
    if (depth <= 0) return out;
    out.reserve(std::min(depth, 64));
    for (int i = best_bid_; i != kNone && static_cast<int>(out.size()) < depth; i = occupied_.find_prev(i - 1))
        out.push_back({index_price(i), level_qty_[i]});
    return out;
}

std::vector<Level> OrderBookArray::get_asks(int depth) const {
    std::vector<Level> out;
    if (depth <= 0) return out;
    out.reserve(std::min(depth, 64));
    for (int i = best_ask_; i != kNone && static_cast<int>(out.size()) < depth; i = occupied_.find_next(i + 1))
        out.push_back({index_price(i), level_qty_[i]});
    return out;
}
