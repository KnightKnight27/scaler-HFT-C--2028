
#include "heap_book.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace orderbook {

bool HeapOrderBook::add_order(
    OrderId id, Side side, Price price, Quantity qty
) {
    if (id == 0 || qty <= 0 || used_ids_.count(id) != 0) {
        return false;
    }

    if (side == Side::Buy) {
        if (price < MIN_BID || price > MAX_BID) {
            return false;
        }
    } else if (side == Side::Sell) {
        if (price < MIN_ASK || price > MAX_ASK) {
            return false;
        }
    } else {
        return false;
    }

    auto& levels =
        (side == Side::Buy) ? bid_levels_ : ask_levels_;

    auto level_it = levels.find(price);

    if (level_it == levels.end()) {
        PriceLevel new_level;
        new_level.generation = next_generation_++;

        auto inserted = levels.emplace(
            price, std::move(new_level)
        );
        level_it = inserted.first;

        const HeapEntry entry{
            price, level_it->second.generation
        };

        if (side == Side::Buy) {
            bid_heap_.push(entry);
        } else {
            ask_heap_.push(entry);
        }
    }

    PriceLevel& level = level_it->second;

    level.fifo.push_back(id);
    auto fifo_it = std::prev(level.fifo.end());

    level.total_qty += qty;

    active_orders_.emplace(
        id, ActiveOrder{side, price, qty, fifo_it}
    );

    used_ids_.insert(id);

    return true;
}

bool HeapOrderBook::cancel_order(OrderId id) {
    auto order_it = active_orders_.find(id);

    if (order_it == active_orders_.end()) {
        return false;
    }

    const ActiveOrder order = order_it->second;

    auto& levels =
        (order.side == Side::Buy)
            ? bid_levels_
            : ask_levels_;

    auto level_it = levels.find(order.price);

    if (level_it == levels.end()) {
        return false;
    }

    PriceLevel& level = level_it->second;

    level.fifo.erase(order.fifo_it);
    level.total_qty -= order.qty;

    active_orders_.erase(order_it);

    // Leave the heap entry behind. It will be discarded lazily.
    if (level.fifo.empty()) {
        levels.erase(level_it);
    }

    return true;
}

void HeapOrderBook::clean_bid_heap() const {
    while (!bid_heap_.empty()) {
        const HeapEntry entry = bid_heap_.top();
        const auto it = bid_levels_.find(entry.price);

        if (it != bid_levels_.end() &&
            it->second.generation == entry.generation) {
            break;
        }

        bid_heap_.pop();
    }
}

void HeapOrderBook::clean_ask_heap() const {
    while (!ask_heap_.empty()) {
        const HeapEntry entry = ask_heap_.top();
        const auto it = ask_levels_.find(entry.price);

        if (it != ask_levels_.end() &&
            it->second.generation == entry.generation) {
            break;
        }

        ask_heap_.pop();
    }
}

BBO HeapOrderBook::get_bbo() const {
    BBO result{};

    clean_bid_heap();
    clean_ask_heap();

    if (!bid_heap_.empty()) {
        const Price price = bid_heap_.top().price;

        result.has_bid = true;
        result.bid_price = price;
        result.bid_qty = bid_levels_.at(price).total_qty;
    }

    if (!ask_heap_.empty()) {
        const Price price = ask_heap_.top().price;

        result.has_ask = true;
        result.ask_price = price;
        result.ask_qty = ask_levels_.at(price).total_qty;
    }

    return result;
}

std::vector<Level> HeapOrderBook::make_depth(
    const std::unordered_map<Price, PriceLevel>& levels,
    std::size_t depth,
    bool bids
) const {
    std::vector<Level> result;
    result.reserve(std::min(depth, levels.size()));

    if (bids) {
        BidHeap heap = bid_heap_;

        while (!heap.empty() && result.size() < depth) {
            const HeapEntry entry = heap.top();
            heap.pop();

            const auto it = levels.find(entry.price);
            if (it == levels.end() ||
                it->second.generation != entry.generation) {
                continue;
            }
            result.push_back(
                Level{entry.price, it->second.total_qty}
            );
        }
    } else {
        AskHeap heap = ask_heap_;

        while (!heap.empty() && result.size() < depth) {
            const HeapEntry entry = heap.top();
            heap.pop();
            const auto it = levels.find(entry.price);
            if (it == levels.end() ||
                it->second.generation != entry.generation) {
                continue;
            }

            result.push_back(
                Level{entry.price, it->second.total_qty}
            );
        }
    }

    return result;
}

std::vector<Level> HeapOrderBook::get_bids(
    std::size_t depth
) const {
    return make_depth(bid_levels_, depth, true);
}

std::vector<Level> HeapOrderBook::get_asks(
    std::size_t depth
) const {
    return make_depth(ask_levels_, depth, false);
}

}
