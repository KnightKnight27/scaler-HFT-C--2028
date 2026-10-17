#include "impl_c/orderbook_c.hpp"

#include <iterator>
#include <limits>
#include <stdexcept>

namespace orderbook {

std::size_t OrderBookC::index_of(Side side, PriceTicks price) noexcept {
    return static_cast<std::size_t>(price - (side == Side::bid ? min_bid_ticks : min_ask_ticks));
}

PriceTicks OrderBookC::price_of(Side side, std::size_t index) noexcept {
    return (side == Side::bid ? min_bid_ticks : min_ask_ticks) + static_cast<PriceTicks>(index);
}

unsigned OrderBookC::bit_index(std::uint64_t word, bool highest) noexcept {
    unsigned index = 0;
    for (unsigned shift = 32; shift > 0; shift /= 2) {
        if (highest ? (word >> shift) != 0
                    : (word & ((std::uint64_t{1} << shift) - 1)) == 0) {
            word >>= shift;
            index += shift;
        }
    }
    return index;
}

std::optional<std::size_t> OrderBookC::best_index(const Bitmap& bits, Side side) noexcept {
    for (std::size_t offset = 0; offset < word_count; ++offset) {
        const auto w = side == Side::bid ? word_count - 1 - offset : offset;
        const auto word = bits[w] & (w == word_count - 1 ? last_mask : ~std::uint64_t{0});
        if (word != 0) return w * 64 + bit_index(word, side == Side::bid);
    }
    return std::nullopt;
}

AddResult OrderBookC::add_order(const Order& order) {
    if (order.side != Side::bid && order.side != Side::ask) return AddResult::invalid_side;
    const bool bid = order.side == Side::bid;
    if (order.price < (bid ? min_bid_ticks : min_ask_ticks) ||
        order.price > (bid ? max_bid_ticks : max_ask_ticks)) return AddResult::invalid_price;
    if (order.quantity <= 0) return AddResult::invalid_quantity;
    if (used_ids_.find(order.id) != used_ids_.end()) return AddResult::duplicate_id;

    const auto index = index_of(order.side, order.price);
    auto& level = (bid ? bids_ : asks_)[index];
    if (order.quantity > std::numeric_limits<Quantity>::max() - level.total)
        return AddResult::quantity_overflow;

    // Allocate nodes/index records before committing totals or occupancy.
    level.orders.push_back(order);
    bool recorded = false;
    try {
        recorded = used_ids_.insert(order.id).second;
        if (!recorded) throw std::logic_error("OrderBookC ID history disagrees");
        if (!live_.emplace(order.id, Locator{order.side, index, std::prev(level.orders.end())}).second)
            throw std::logic_error("OrderBookC live index disagrees");
    } catch (...) {
        if (recorded) used_ids_.erase(order.id);
        level.orders.pop_back();
        throw;
    }
    level.total += order.quantity;
    (bid ? bid_bits_ : ask_bits_)[index / 64] |= std::uint64_t{1} << (index % 64);
    return AddResult::added;
}

CancelResult OrderBookC::cancel_order(OrderId id) {
    const auto found = live_.find(id);
    if (found == live_.end()) return CancelResult::not_found;
    const auto loc = found->second;
    const bool bid = loc.side == Side::bid;
    auto& level = (bid ? bids_ : asks_)[loc.index];
    level.total -= loc.order->quantity;
    level.orders.erase(loc.order);
    live_.erase(found);
    if (level.orders.empty())
        (bid ? bid_bits_ : ask_bits_)[loc.index / 64] &= ~(std::uint64_t{1} << (loc.index % 64));
    // The fixed empty Level remains, with zero total and clear occupancy.
    return CancelResult::cancelled;
}

Bbo OrderBookC::get_bbo() const {
    Bbo result;
    const auto bid = best_index(bid_bits_, Side::bid);
    const auto ask = best_index(ask_bits_, Side::ask);
    if (bid) result.bid = PriceLevel{price_of(Side::bid, *bid), bids_[*bid].total};
    if (ask) result.ask = PriceLevel{price_of(Side::ask, *ask), asks_[*ask].total};
    return result;
}

DepthSnapshot OrderBookC::depth(Side side, Depth count) const {
    DepthSnapshot result;
    if (count <= 0) return result;
    const auto& bits = side == Side::bid ? bid_bits_ : ask_bits_;
    const auto& levels = side == Side::bid ? bids_ : asks_;
    for (std::size_t offset = 0; offset < word_count && count > 0; ++offset) {
        const auto w = side == Side::bid ? word_count - 1 - offset : offset;
        auto word = bits[w] & (w == word_count - 1 ? last_mask : ~std::uint64_t{0});
        while (word != 0 && count > 0) {
            const auto bit = bit_index(word, side == Side::bid);
            const auto index = w * 64 + bit;
            result.push_back({price_of(side, index), levels[index].total});
            word &= ~(std::uint64_t{1} << bit);
            --count;
        }
    }
    return result;
}

DepthSnapshot OrderBookC::get_bids(Depth count) const { return depth(Side::bid, count); }
DepthSnapshot OrderBookC::get_asks(Depth count) const { return depth(Side::ask, count); }
} // namespace orderbook
