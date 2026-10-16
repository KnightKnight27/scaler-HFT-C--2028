#pragma once
#include "types.hpp"
#include <array>
#include <unordered_map>

namespace ob {
// C: a single price-level buffer. Even slots are descending bids; odd slots are
// ascending asks. Slots 0 and 1 always hold the current best levels when present.
class InterleavedBook {
public:
    static constexpr std::size_t storage_alignment = 128;
    struct StorageView {
        const Level* data;
        std::size_t bid_count;
        std::size_t ask_count;
    };
private:
    alignas(storage_alignment) std::array<Level, 2 * levels_per_side> levels_{};
    // Metadata only: price tick -> rank within its side, or -1 if absent.
    std::array<int, 2 * levels_per_side> ranks_{};
    std::array<std::size_t, 2> counts_{};
    std::unordered_map<std::uint64_t, Order> orders_;

    static std::size_t side_index(Side side) { return side == Side::Buy ? 0 : 1; }
    static std::size_t price_index(Side side, Tick price) {
        return side_index(side) * levels_per_side + static_cast<std::size_t>(price - base_tick(side));
    }
    Level& level(Side side, std::size_t rank) { return levels_[2 * rank + side_index(side)]; }
    const Level& level(Side side, std::size_t rank) const { return levels_[2 * rank + side_index(side)]; }
    // Ordinary, self-implemented lower-bound search; no branchless-code claim.
    std::size_t insertion_rank(Side side, Tick price) const {
        std::size_t first = 0, last = counts_[side_index(side)];
        while (first < last) {
            const auto middle = first + (last - first) / 2;
            const bool before = side == Side::Buy ? level(side, middle).price > price
                                                  : level(side, middle).price < price;
            if (before) first = middle + 1;
            else last = middle;
        }
        return first;
    }
    std::vector<Level> depth_levels(Side side, int depth) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        const auto count = std::min(static_cast<std::size_t>(depth), counts_[side_index(side)]);
        result.reserve(count);
        for (std::size_t rank = 0; rank < count; ++rank) result.push_back(level(side, rank));
        return result;
    }
public:
    InterleavedBook() { ranks_.fill(-1); }
    void add_order(std::uint64_t id, Side side, Tick price, long qty) {
        validate(side, price, qty);
        const auto key = price_index(side, price);
        const int old_rank = ranks_[key];
        check_sum(old_rank >= 0 ? level(side, static_cast<std::size_t>(old_rank)).qty : 0, qty);
        if (!orders_.emplace(id, Order{side, price, qty}).second)
            throw std::invalid_argument("duplicate live order ID");
        if (old_rank >= 0) {
            level(side, static_cast<std::size_t>(old_rank)).qty += qty;
            return;
        }
        const auto position = insertion_rank(side, price);
        auto& count = counts_[side_index(side)];
        // There are exactly 5,000 valid prices on each side; a new level always fits.
        // Shifting only that side's parity leaves the other side untouched.
        for (auto rank = count; rank > position; --rank) {
            level(side, rank) = level(side, rank - 1);
            ranks_[price_index(side, level(side, rank).price)] = static_cast<int>(rank);
        }
        level(side, position) = Level{price, qty};
        ranks_[key] = static_cast<int>(position);
        ++count;
    }
    bool cancel_order(std::uint64_t id) {
        const auto found = orders_.find(id);
        if (found == orders_.end()) return false;
        const auto order = found->second;
        const auto key = price_index(order.side, order.price);
        const auto position = static_cast<std::size_t>(ranks_[key]);
        auto& current = level(order.side, position);
        current.qty -= order.qty;
        if (current.qty == 0) {
            auto& count = counts_[side_index(order.side)];
            for (auto rank = position; rank + 1 < count; ++rank) {
                level(order.side, rank) = level(order.side, rank + 1);
                ranks_[price_index(order.side, level(order.side, rank).price)] = static_cast<int>(rank);
            }
            --count;
            level(order.side, count) = Level{};
            ranks_[key] = -1;
        }
        orders_.erase(found);
        return true;
    }
    BBO get_bbo() const {
        BBO result;
        if (counts_[0]) {
            result.has_bid = true; result.bid_price = levels_[0].price; result.bid_qty = levels_[0].qty;
        }
        if (counts_[1]) {
            result.has_ask = true; result.ask_price = levels_[1].price; result.ask_qty = levels_[1].qty;
        }
        return result;
    }
    std::vector<Level> get_bids(int depth = 10) const { return depth_levels(Side::Buy, depth); }
    std::vector<Level> get_asks(int depth = 10) const { return depth_levels(Side::Sell, depth); }

    // Read-only view for physical-layout checks; it does not create a second price buffer.
    StorageView storage_view() const noexcept { return {levels_.data(), counts_[0], counts_[1]}; }
};
static_assert(alignof(InterleavedBook) >= InterleavedBook::storage_alignment, "price buffer alignment");
static_assert(2 * sizeof(Level) <= 64, "the two top levels must fit a 64-byte line");
} // namespace ob
