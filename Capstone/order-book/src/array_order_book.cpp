#include "array_order_book.hpp"

#include <algorithm>

ArrayOrderBook::ArrayOrderBook(std::size_t expected_orders)
    : level_qty_(kNumLevels, 0), orders_(expected_orders) {}

void ArrayOrderBook::mark_non_empty(int index) {
    non_empty_[index >> 6] |= std::uint64_t{1} << (index & 63);
}

void ArrayOrderBook::mark_empty(int index) {
    non_empty_[index >> 6] &= ~(std::uint64_t{1} << (index & 63));
}

// Highest non-empty level at or below `index`, or kNoBid if there is none.
// Bids sit below asks in the array, so starting from a bid this only finds bids.
int ArrayOrderBook::highest_non_empty_at_or_below(int index) const {
    if (index < 0) return kNoBid;
    int word = index >> 6;
    // Keep bits 0..(index % 64) of the first word: the levels at or below `index`.
    std::uint64_t bits = non_empty_[word] & (~std::uint64_t{0} >> (63 - (index & 63)));
    while (bits == 0) {
        if (--word < 0) return kNoBid;
        bits = non_empty_[word];
    }
    return word * 64 + 63 - __builtin_clzll(bits);  // position of the highest set bit
}

// Lowest non-empty level at or above `index`, or kNoAsk if there is none.
// Starting from an ask this only finds asks.
int ArrayOrderBook::lowest_non_empty_at_or_above(int index) const {
    if (index >= kNumLevels) return kNoAsk;
    int word = index >> 6;
    // Keep bits (index % 64)..63 of the first word: the levels at or above `index`.
    std::uint64_t bits = non_empty_[word] & (~std::uint64_t{0} << (index & 63));
    while (bits == 0) {
        if (++word == kBitmapWords) return kNoAsk;
        bits = non_empty_[word];
    }
    return word * 64 + __builtin_ctzll(bits);  // position of the lowest set bit
}

bool ArrayOrderBook::add_order(std::uint64_t id, Side side, double price, long qty) {
    Tick tick = 0;
    if (!validate_order(side, price, qty, tick)) return false;
    if (!orders_.insert(id, tick, static_cast<std::int32_t>(qty))) return false;  // id already live

    const int index = index_of(tick);
    if (level_qty_[index] == 0) mark_non_empty(index);
    level_qty_[index] += qty;

    if (side == Side::Buy) {
        best_bid_ = std::max(best_bid_, index);
    } else {
        best_ask_ = std::min(best_ask_, index);
    }
    return true;
}

bool ArrayOrderBook::cancel_order(std::uint64_t id) {
    OrderTable::Slot* order = orders_.find(id);
    if (order == nullptr) return false;

    const int index = index_of(order->tick);
    level_qty_[index] -= order->qty;
    orders_.erase(order);

    if (level_qty_[index] == 0) {
        mark_empty(index);
        // The best price only moves when the best level itself becomes empty.
        if (index == best_bid_) best_bid_ = highest_non_empty_at_or_below(index - 1);
        if (index == best_ask_) best_ask_ = lowest_non_empty_at_or_above(index + 1);
    }
    return true;
}

BBO ArrayOrderBook::get_bbo() const {
    BBO bbo{};
    if (best_bid_ != kNoBid) {
        bbo.has_bid = true;
        bbo.bid_price = price_at(best_bid_);
        bbo.bid_qty = level_qty_[best_bid_];
    }
    if (best_ask_ != kNoAsk) {
        bbo.has_ask = true;
        bbo.ask_price = price_at(best_ask_);
        bbo.ask_qty = level_qty_[best_ask_];
    }
    return bbo;
}

std::vector<Level> ArrayOrderBook::get_bids(int depth) const {
    std::vector<Level> out;
    if (depth <= 0) return out;
    out.reserve(std::min(depth, kLevelsPerSide));
    for (int i = best_bid_; i != kNoBid && static_cast<int>(out.size()) < depth;
         i = highest_non_empty_at_or_below(i - 1)) {
        out.push_back({price_at(i), level_qty_[i]});
    }
    return out;
}

std::vector<Level> ArrayOrderBook::get_asks(int depth) const {
    std::vector<Level> out;
    if (depth <= 0) return out;
    out.reserve(std::min(depth, kLevelsPerSide));
    for (int i = best_ask_; i != kNoAsk && static_cast<int>(out.size()) < depth;
         i = lowest_non_empty_at_or_above(i + 1)) {
        out.push_back({price_at(i), level_qty_[i]});
    }
    return out;
}
