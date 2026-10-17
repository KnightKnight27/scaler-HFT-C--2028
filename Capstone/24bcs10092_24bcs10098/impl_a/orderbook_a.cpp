#include "impl_a/orderbook_a.hpp"

#include <iterator>
#include <limits>
#include <stdexcept>

namespace orderbook {
namespace {

template <typename Iterator>
DepthSnapshot snapshot(Iterator first, Iterator last, Depth depth) {
    DepthSnapshot result;
    if (depth <= 0) {
        return result;
    }
    for (; first != last && depth > 0; ++first, --depth) {
        result.push_back({first->first, first->second.total});
    }
    return result;
}

} // namespace

AddResult OrderBookA::add_order(const Order& order) {
    if (order.side != Side::bid && order.side != Side::ask) {
        return AddResult::invalid_side;
    }
    const bool bid = order.side == Side::bid;
    if (order.price < (bid ? min_bid_ticks : min_ask_ticks) ||
        order.price > (bid ? max_bid_ticks : max_ask_ticks)) {
        return AddResult::invalid_price;
    }
    if (order.quantity <= 0) {
        return AddResult::invalid_quantity;
    }
    if (used_ids_.find(order.id) != used_ids_.end()) {
        return AddResult::duplicate_id;
    }

    auto& levels = bid ? bids_ : asks_;
    auto level = levels.find(order.price);
    const Quantity total = level == levels.end() ? 0 : level->second.total;
    if (order.quantity > std::numeric_limits<Quantity>::max() - total) {
        return AddResult::quantity_overflow;
    }

    // Only allocations can fail below. Roll back each completed insertion;
    // commit the aggregate after both indexes and the FIFO node exist.
    bool new_level = false;
    if (level == levels.end()) {
        level = levels.try_emplace(order.price).first;
        new_level = true;
    }
    bool appended = false;
    bool recorded = false;
    try {
        level->second.orders.push_back(order);
        appended = true;
        recorded = used_ids_.insert(order.id).second;
        if (!recorded) {
            throw std::logic_error("OrderBookA accepted-ID history disagrees");
        }
        const auto node = std::prev(level->second.orders.end());
        const auto inserted = live_.emplace(order.id, Locator{order.side, level, node});
        if (!inserted.second) {
            throw std::logic_error("OrderBookA ID indexes disagree");
        }
    } catch (...) {
        if (recorded) {
            used_ids_.erase(order.id);
        }
        if (appended) {
            level->second.orders.pop_back();
        }
        if (new_level) {
            levels.erase(level);
        }
        throw;
    }
    level->second.total += order.quantity;
    return AddResult::added;
}

CancelResult OrderBookA::cancel_order(OrderId id) {
    const auto found = live_.find(id);
    if (found == live_.end()) {
        return CancelResult::not_found;
    }
    const Locator loc = found->second;
    auto& levels = loc.side == Side::bid ? bids_ : asks_;
    auto& level = loc.level->second;
    level.total -= loc.order->quantity;
    level.orders.erase(loc.order);
    live_.erase(found);
    if (level.orders.empty()) {
        levels.erase(loc.level);
    }
    // used_ids_ intentionally retains the cancelled ID.
    return CancelResult::cancelled;
}

Bbo OrderBookA::get_bbo() const {
    Bbo result;
    if (!bids_.empty()) {
        result.bid = PriceLevel{bids_.rbegin()->first, bids_.rbegin()->second.total};
    }
    if (!asks_.empty()) {
        result.ask = PriceLevel{asks_.begin()->first, asks_.begin()->second.total};
    }
    return result;
}

DepthSnapshot OrderBookA::get_bids(Depth depth) const {
    return snapshot(bids_.rbegin(), bids_.rend(), depth);
}

DepthSnapshot OrderBookA::get_asks(Depth depth) const {
    return snapshot(asks_.begin(), asks_.end(), depth);
}

} // namespace orderbook
