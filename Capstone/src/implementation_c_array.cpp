#include "implementation_c_array.hpp"

#include <algorithm>
#include <stdexcept>

namespace {
constexpr int kBidMin = 5000;
constexpr int kAskMin = 10001;
constexpr int kSideLevels = 5000;
constexpr int kTotalLevels = 10000;

void check_depth(int depth) {
    if (depth < 0) throw std::invalid_argument("depth cannot be negative");
}
} // namespace

ArrayOrderBook::OrderTable::OrderTable() : slots_(1024) {}

std::uint64_t ArrayOrderBook::OrderTable::hash(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

void ArrayOrderBook::OrderTable::rehash(std::size_t capacity) {
    std::vector<Order> old = std::move(slots_);
    slots_.assign(capacity, Order{});
    size_ = 0;
    deleted_ = 0;
    for (const Order& order : old) {
        if (order.state == 1) insert(order.id, order.side, order.price, order.qty);
    }
}

bool ArrayOrderBook::OrderTable::insert(std::uint64_t id, Side side, int price, long qty) {
    if ((size_ + deleted_ + 1) * 10 >= slots_.size() * 7)
        rehash(slots_.size() * 2);
    const std::size_t mask = slots_.size() - 1;
    std::size_t index = static_cast<std::size_t>(hash(id)) & mask;
    std::size_t first_deleted = slots_.size();
    for (;;) {
        Order& slot = slots_[index];
        if (slot.state == 0) {
            if (first_deleted != slots_.size()) index = first_deleted;
            Order& target = slots_[index];
            target = Order{id, price, qty, side, 1};
            ++size_;
            if (first_deleted != slots_.size()) --deleted_;
            return true;
        }
        if (slot.state == 2) {
            if (first_deleted == slots_.size()) first_deleted = index;
        } else if (slot.id == id) {
            return false;
        }
        index = (index + 1) & mask;
    }
}

ArrayOrderBook::Order* ArrayOrderBook::OrderTable::find(std::uint64_t id) {
    const std::size_t mask = slots_.size() - 1;
    std::size_t index = static_cast<std::size_t>(hash(id)) & mask;
    for (;;) {
        Order& slot = slots_[index];
        if (slot.state == 0) return nullptr;
        if (slot.state == 1 && slot.id == id) return &slot;
        index = (index + 1) & mask;
    }
}

bool ArrayOrderBook::OrderTable::erase(std::uint64_t id) {
    Order* slot = find(id);
    if (!slot) return false;
    slot->state = 2;
    --size_;
    ++deleted_;
    if (deleted_ > size_ && slots_.size() > 1024) rehash(slots_.size());
    return true;
}

ArrayOrderBook::ArrayOrderBook() : levels_(kTotalLevels) {}

int ArrayOrderBook::level_index(Side side, int price) noexcept {
    return side == Side::Buy ? price - kBidMin : kSideLevels + price - kAskMin;
}

int ArrayOrderBook::index_price(int index) noexcept {
    return index < kSideLevels ? kBidMin + index : kAskMin + index - kSideLevels;
}

void ArrayOrderBook::add_order(std::uint64_t id, Side side, double price, long qty) {
    if (qty <= 0) throw std::invalid_argument("quantity must be positive");
    const int ticks = price_to_ticks(price, side);
    if (!orders_.insert(id, side, ticks, qty))
        throw std::invalid_argument("duplicate order ID");
    const int index = level_index(side, ticks);
    levels_[index] += qty;
    if (side == Side::Buy) best_bid_ = std::max(best_bid_, index);
    else if (best_ask_ == -1 || index < best_ask_) best_ask_ = index;
}

bool ArrayOrderBook::cancel_order(std::uint64_t id) {
    Order* order = orders_.find(id);
    if (!order) return false;
    const int index = level_index(order->side, order->price);
    levels_[index] -= order->qty;
    const bool was_bid = order->side == Side::Buy;
    orders_.erase(id);
    if (levels_[index] == 0) {
        if (was_bid && index == best_bid_) repair_best_bid();
        if (!was_bid && index == best_ask_) repair_best_ask();
    }
    return true;
}

void ArrayOrderBook::repair_best_bid() {
    while (best_bid_ >= 0 && levels_[best_bid_] == 0) --best_bid_;
}

void ArrayOrderBook::repair_best_ask() {
    if (best_ask_ < kSideLevels) best_ask_ = kSideLevels;
    while (best_ask_ < kTotalLevels && levels_[best_ask_] == 0) ++best_ask_;
    if (best_ask_ == kTotalLevels) best_ask_ = -1;
}

BBO ArrayOrderBook::get_bbo() const {
    BBO out;
    if (best_bid_ != -1) {
        out.has_bid = true;
        out.bid_price = ticks_to_price(index_price(best_bid_));
        out.bid_qty = levels_[best_bid_];
    }
    if (best_ask_ != -1) {
        out.has_ask = true;
        out.ask_price = ticks_to_price(index_price(best_ask_));
        out.ask_qty = levels_[best_ask_];
    }
    return out;
}

std::vector<Level> ArrayOrderBook::get_bids(int depth) const {
    check_depth(depth);
    std::vector<Level> result;
    result.reserve(static_cast<std::size_t>(std::min(depth, kSideLevels)));
    for (int i = best_bid_; i >= 0 && static_cast<int>(result.size()) < depth; --i)
        if (levels_[i] != 0) result.push_back({ticks_to_price(index_price(i)), levels_[i]});
    return result;
}

std::vector<Level> ArrayOrderBook::get_asks(int depth) const {
    check_depth(depth);
    std::vector<Level> result;
    result.reserve(static_cast<std::size_t>(std::min(depth, kSideLevels)));
    if (best_ask_ == -1) return result;
    for (int i = best_ask_; i < kTotalLevels && static_cast<int>(result.size()) < depth; ++i)
        if (levels_[i] != 0) result.push_back({ticks_to_price(index_price(i)), levels_[i]});
    return result;
}
