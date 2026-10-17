#include "sorted_vector_order_book.hpp"

#include <algorithm>
#include <functional>

SortedVectorOrderBook::SortedVectorOrderBook(std::size_t expected_orders) {
    // A side never has more than 5000 levels, so these vectors never reallocate.
    bids_.reserve(kLevelsPerSide);
    asks_.reserve(kLevelsPerSide);
    // Tombstones never outnumber live orders, so the vector holds at most about
    // twice the number of live orders.
    orders_.reserve(2 * expected_orders);
}

// First level whose price is not worse than `tick`: the level itself if it
// exists, otherwise the position where it would be inserted.
template <class Worse>
SortedVectorOrderBook::Levels::iterator SortedVectorOrderBook::find_level(Levels& levels, Tick tick) {
    return std::lower_bound(levels.begin(), levels.end(), tick,
                            [](const LevelEntry& level, Tick t) { return Worse{}(level.tick, t); });
}

template <class Worse>
void SortedVectorOrderBook::add_to_level(Levels& levels, Tick tick, long qty) {
    const auto it = find_level<Worse>(levels, tick);
    if (it != levels.end() && it->tick == tick) {
        it->qty += qty;
    } else {
        levels.insert(it, LevelEntry{tick, qty});  // moves every better level one slot back
    }
}

template <class Worse>
void SortedVectorOrderBook::remove_from_level(Levels& levels, Tick tick, long qty) {
    const auto it = find_level<Worse>(levels, tick);  // exists, because a live order rests on it
    it->qty -= qty;
    if (it->qty == 0) levels.erase(it);
}

std::vector<Level> SortedVectorOrderBook::best_levels(const Levels& levels, int depth) {
    std::vector<Level> out;
    if (depth <= 0) return out;
    const std::size_t count = std::min(static_cast<std::size_t>(depth), levels.size());
    out.reserve(count);
    auto it = levels.rbegin();  // the best price is at the back
    for (std::size_t i = 0; i < count; ++i, ++it) {
        out.push_back({tick_to_price(it->tick), it->qty});
    }
    return out;
}

SortedVectorOrderBook::Orders::iterator SortedVectorOrderBook::find_order(std::uint64_t id) {
    return std::lower_bound(orders_.begin(), orders_.end(), id,
                            [](const OrderEntry& order, std::uint64_t key) { return order.id < key; });
}

void SortedVectorOrderBook::compact_orders() {
    orders_.erase(std::remove_if(orders_.begin(), orders_.end(),
                                 [](const OrderEntry& order) { return order.qty == 0; }),
                  orders_.end());
    tombstones_ = 0;
}

bool SortedVectorOrderBook::add_order(std::uint64_t id, Side side, double price, long qty) {
    Tick tick = 0;
    if (!validate_order(side, price, qty, tick)) return false;
    const OrderEntry entry{id, tick, static_cast<std::int32_t>(qty)};

    if (orders_.empty() || id > orders_.back().id) {
        orders_.push_back(entry);  // the usual case: ids only go up
    } else {
        const auto it = find_order(id);
        if (it != orders_.end() && it->id == id) {
            if (it->qty != 0) return false;  // id already live
            *it = entry;                      // reuse the tombstone of the cancelled order
            --tombstones_;
        } else {
            orders_.insert(it, entry);  // an id lower than the newest one: shifts the tail, O(n)
        }
    }

    if (side == Side::Buy) {
        add_to_level<std::less<Tick>>(bids_, tick, qty);
    } else {
        add_to_level<std::greater<Tick>>(asks_, tick, qty);
    }
    return true;
}

bool SortedVectorOrderBook::cancel_order(std::uint64_t id) {
    const auto it = find_order(id);
    if (it == orders_.end() || it->id != id || it->qty == 0) return false;

    if (is_bid_tick(it->tick)) {
        remove_from_level<std::less<Tick>>(bids_, it->tick, it->qty);
    } else {
        remove_from_level<std::greater<Tick>>(asks_, it->tick, it->qty);
    }

    it->qty = 0;
    ++tombstones_;
    // Each compaction removes at least half of the vector, so on average it adds
    // O(1) work per cancel. The cost lands on one unlucky cancel, though.
    if (2 * tombstones_ > orders_.size()) compact_orders();
    return true;
}

BBO SortedVectorOrderBook::get_bbo() const {
    BBO bbo{};
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(bids_.back().tick);
        bbo.bid_qty = bids_.back().qty;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(asks_.back().tick);
        bbo.ask_qty = asks_.back().qty;
    }
    return bbo;
}

std::vector<Level> SortedVectorOrderBook::get_bids(int depth) const { return best_levels(bids_, depth); }

std::vector<Level> SortedVectorOrderBook::get_asks(int depth) const { return best_levels(asks_, depth); }
