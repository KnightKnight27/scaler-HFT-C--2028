#include "impl_b/orderbook_b.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace orderbook {

bool OrderBookB::better(const Entry& a, const Entry& b, Side side) noexcept {
    if (a.order.price != b.order.price) {
        return side == Side::bid ? a.order.price > b.order.price
                                 : a.order.price < b.order.price;
    }
    return a.seq < b.seq;
}

void OrderBookB::swap_entries(Heap& heap, std::size_t a, std::size_t b) noexcept {
    std::swap(heap[a], heap[b]);
    // These IDs already exist; find never allocates or inserts. Integral hashes
    // and comparisons do not throw. No iterators are retained across rehashes.
    live_.find(heap[a].order.id)->second.pos = a;
    live_.find(heap[b].order.id)->second.pos = b;
}

void OrderBookB::sift_up(Heap& heap, std::size_t pos, Side side) noexcept {
    while (pos > 0) {
        const auto parent = (pos - 1) / 2;
        if (!better(heap[pos], heap[parent], side)) break;
        swap_entries(heap, pos, parent);
        pos = parent;
    }
}

void OrderBookB::sift_down(Heap& heap, std::size_t pos, Side side) noexcept {
    // pos < size/2 exactly identifies nodes with at least a left child.
    while (pos < heap.size() / 2) {
        auto child = pos * 2 + 1;
        if (child + 1 < heap.size() && better(heap[child + 1], heap[child], side)) ++child;
        if (!better(heap[child], heap[pos], side)) break;
        swap_entries(heap, pos, child);
        pos = child;
    }
}

AddResult OrderBookB::add_order(const Order& order) {
    if (order.side != Side::bid && order.side != Side::ask) return AddResult::invalid_side;
    const bool bid = order.side == Side::bid;
    if (order.price < (bid ? min_bid_ticks : min_ask_ticks) ||
        order.price > (bid ? max_bid_ticks : max_ask_ticks)) return AddResult::invalid_price;
    if (order.quantity <= 0) return AddResult::invalid_quantity;
    if (used_ids_.find(order.id) != used_ids_.end()) return AddResult::duplicate_id;

    auto& heap = bid ? bids_ : asks_;
    auto& totals = bid ? bid_totals_ : ask_totals_;
    const auto found = totals.find(order.price);
    const Quantity total = found == totals.end() ? 0 : found->second;
    if (order.quantity > std::numeric_limits<Quantity>::max() - total)
        return AddResult::quantity_overflow;
    if (next_seq_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("OrderBookB arrival sequence exhausted");

    // vector growth has a strong guarantee for this trivial Entry. Delay all
    // heap movements, totals and sequence changes until allocations finish.
    heap.push_back({order, next_seq_});
    bool new_total = false;
    bool recorded = false;
    Totals::iterator level;
    try {
        const auto inserted = totals.try_emplace(order.price, 0);
        level = inserted.first;
        new_total = inserted.second;
        recorded = used_ids_.insert(order.id).second;
        if (!recorded) throw std::logic_error("OrderBookB ID history disagrees");
        if (!live_.emplace(order.id, Locator{order.side, heap.size() - 1}).second)
            throw std::logic_error("OrderBookB live index disagrees");
    } catch (...) {
        if (recorded) used_ids_.erase(order.id);
        if (new_total) totals.erase(level);
        heap.pop_back();
        throw;
    }
    // From here onward: integer arithmetic, trivial swaps and existing-ID lookup.
    level->second += order.quantity;
    ++next_seq_;
    sift_up(heap, heap.size() - 1, order.side);
    return AddResult::added;
}

CancelResult OrderBookB::cancel_order(OrderId id) {
    const auto found = live_.find(id);
    if (found == live_.end()) return CancelResult::not_found;
    const auto loc = found->second;
    auto& heap = loc.side == Side::bid ? bids_ : asks_;
    auto& totals = loc.side == Side::bid ? bid_totals_ : ask_totals_;
    const Order order = heap[loc.pos].order;
    const auto level = totals.find(order.price);

    swap_entries(heap, loc.pos, heap.size() - 1);
    heap.pop_back();
    live_.erase(found);
    level->second -= order.quantity;
    if (level->second == 0) totals.erase(level);
    if (loc.pos < heap.size()) {
        if (loc.pos > 0 && better(heap[loc.pos], heap[(loc.pos - 1) / 2], loc.side))
            sift_up(heap, loc.pos, loc.side);
        else
            sift_down(heap, loc.pos, loc.side);
    }
    return CancelResult::cancelled;
}

Bbo OrderBookB::get_bbo() const {
    Bbo result;
    if (!bids_.empty()) {
        const auto price = bids_.front().order.price;
        result.bid = PriceLevel{price, bid_totals_.at(price)};
    }
    if (!asks_.empty()) {
        const auto price = asks_.front().order.price;
        result.ask = PriceLevel{price, ask_totals_.at(price)};
    }
    return result;
}

DepthSnapshot OrderBookB::depth(const Totals& totals, Depth count, Side side) {
    DepthSnapshot result;
    if (count <= 0) return result;
    result.reserve(totals.size());
    for (const auto& level : totals) result.push_back({level.first, level.second});
    std::sort(result.begin(), result.end(), [side](const PriceLevel& a, const PriceLevel& b) {
        return side == Side::bid ? a.price > b.price : a.price < b.price;
    });
    // At most 5000 levels: this comparison avoids converting oversized depth.
    if (count < static_cast<Depth>(result.size())) result.resize(static_cast<std::size_t>(count));
    return result;
}

DepthSnapshot OrderBookB::get_bids(Depth count) const { return depth(bid_totals_, count, Side::bid); }
DepthSnapshot OrderBookB::get_asks(Depth count) const { return depth(ask_totals_, count, Side::ask); }

} // namespace orderbook
