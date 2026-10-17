#include "map_book.hpp"

#include <iterator>

namespace orderbook {

bool MapOrderBook::valid_price(Side side, Price price) {
    if (side == Side::Buy) {
        return price >= 5000 && price <= 9999;
    }

    return price >= 10001 && price <= 15000;
}

bool MapOrderBook::add_order(
    OrderId id, Side side, Price price, Quantity qty
) {
    if (id == 0 ||
        qty <= 0 ||
        !valid_price(side, price) ||
        used_ids_.find(id) != used_ids_.end()) {
        return false;
    }

    // An accepted ID can never be reused.
    used_ids_.insert(id);

    if (side == Side::Buy) {
        auto [it, inserted] = bids_.try_emplace(price);
        (void)inserted;

        auto& level = it->second;
        level.fifo.push_back(id);

        auto fifo_it = std::prev(level.fifo.end());
        level.total_qty += qty;

        active_.emplace(
            id, ActiveOrder{side, price, qty, fifo_it}
        );
    } else {
        auto [it, inserted] = asks_.try_emplace(price);
        (void)inserted;

        auto& level = it->second;
        level.fifo.push_back(id);

        auto fifo_it = std::prev(level.fifo.end());
        level.total_qty += qty;

        active_.emplace(
            id, ActiveOrder{side, price, qty, fifo_it}
        );
    }

    return true;
}

bool MapOrderBook::cancel_order(OrderId id) {
    auto order_it = active_.find(id);

    if (order_it == active_.end()) {
        return false;
    }

    const ActiveOrder order = order_it->second;

    if (order.side == Side::Buy) {
        auto level_it = bids_.find(order.price);
        auto& level = level_it->second;

        level.total_qty -= order.qty;
        level.fifo.erase(order.fifo_it);

        if (level.fifo.empty()) {
            bids_.erase(level_it);
        }
    } else {
        auto level_it = asks_.find(order.price);
        auto& level = level_it->second;

        level.total_qty -= order.qty;
        level.fifo.erase(order.fifo_it);

        if (level.fifo.empty()) {
            asks_.erase(level_it);
        }
    }

    active_.erase(order_it);
    return true;
}

BBO MapOrderBook::get_bbo() const {
    BBO result;

    if (!bids_.empty()) {
        result.has_bid = true;
        result.bid_price = bids_.begin()->first;
        result.bid_qty = bids_.begin()->second.total_qty;
    }

    if (!asks_.empty()) {
        result.has_ask = true;
        result.ask_price = asks_.begin()->first;
        result.ask_qty = asks_.begin()->second.total_qty;
    }

    return result;
}

std::vector<Level> MapOrderBook::get_bids(
    std::size_t depth
) const {
    std::vector<Level> result;
    result.reserve(depth);

    for (auto it = bids_.begin();
         it != bids_.end() && result.size() < depth;
         ++it) {
        result.push_back({it->first, it->second.total_qty});
    }

    return result;
}

std::vector<Level> MapOrderBook::get_asks(
    std::size_t depth
) const {
    std::vector<Level> result;
    result.reserve(depth);

    for (auto it = asks_.begin();
         it != asks_.end() && result.size() < depth;
         ++it) {
        result.push_back({it->first, it->second.total_qty});
    }

    return result;
}

}