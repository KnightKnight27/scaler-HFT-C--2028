#pragma once
#include "types.hpp"
#include <algorithm>

namespace ob {
// B: sorted contiguous order history and price vectors. Cancelled slots are tombstones.
class VectorBook {
    struct Slot { std::uint64_t id; Order order; bool live; };
    std::vector<Slot> orders_;
    std::vector<Level> bids_, asks_;
    std::vector<Level>& levels(Side s) { return s == Side::Buy ? bids_ : asks_; }
public:
    VectorBook() { bids_.reserve(levels_per_side); asks_.reserve(levels_per_side); }
    void add_order(std::uint64_t id, Side side, Tick price, long qty) {
        validate(side, price, qty);
        // Increasing IDs take the append path; arbitrary unique IDs remain supported.
        auto pos = orders_.end();
        if (!orders_.empty() && id <= orders_.back().id) {
            pos = std::lower_bound(orders_.begin(), orders_.end(), id,
                [](const Slot& slot, std::uint64_t key) { return slot.id < key; });
            if (pos != orders_.end() && pos->id == id)
                throw std::invalid_argument("order ID already used");
        }
        auto& side_levels = levels(side);
        auto level = std::lower_bound(side_levels.begin(), side_levels.end(), price,
            [](const Level& l, Tick key) { return l.price < key; });
        const bool exists = level != side_levels.end() && level->price == price;
        check_sum(exists ? level->qty : 0, qty);
        orders_.insert(pos, Slot{id, Order{side, price, qty}, true});
        // Price capacity is fixed in the constructor, so this insert cannot allocate.
        if (exists) level->qty += qty;
        else side_levels.insert(level, Level{price, qty});
    }
    bool cancel_order(std::uint64_t id) {
        auto order = std::lower_bound(orders_.begin(), orders_.end(), id,
            [](const Slot& slot, std::uint64_t key) { return slot.id < key; });
        if (order == orders_.end() || order->id != id || !order->live) return false;
        auto& side_levels = levels(order->order.side);
        auto level = std::lower_bound(side_levels.begin(), side_levels.end(), order->order.price,
            [](const Level& l, Tick key) { return l.price < key; });
        level->qty -= order->order.qty;
        if (level->qty == 0) side_levels.erase(level);
        order->live = false;
        return true;
    }
    BBO get_bbo() const {
        BBO result;
        if (!bids_.empty()) {
            const auto& l = bids_.back();
            result.has_bid = true; result.bid_price = l.price; result.bid_qty = l.qty;
        }
        if (!asks_.empty()) {
            const auto& l = asks_.front();
            result.has_ask = true; result.ask_price = l.price; result.ask_qty = l.qty;
        }
        return result;
    }
    std::vector<Level> get_bids(int depth = 10) const {
        if (depth <= 0) return {};
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(depth), bids_.size());
        return {bids_.rbegin(), bids_.rbegin() + static_cast<std::ptrdiff_t>(count)};
    }
    std::vector<Level> get_asks(int depth = 10) const {
        if (depth <= 0) return {};
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(depth), asks_.size());
        return {asks_.begin(), asks_.begin() + static_cast<std::ptrdiff_t>(count)};
    }
};
} // namespace ob
