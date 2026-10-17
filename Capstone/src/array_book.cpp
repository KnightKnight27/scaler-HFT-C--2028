
#include "array_book.hpp"

#include <algorithm>
#include <iterator>

namespace orderbook {

bool ArrayOrderBook::valid_price(Side side, Price price) {
    if (side == Side::Buy) {
        return price >= MIN_BID && price <= MAX_BID;
    }

    if (side == Side::Sell) {
        return price >= MIN_ASK && price <= MAX_ASK;
    }
    return false;
}

bool ArrayOrderBook::add_order(
    OrderId id, Side side, Price price, Quantity qty
) {
    if (id == 0 || qty <= 0 ||
        used_ids_.count(id) != 0 ||
        !valid_price(side, price)) {
        return false;
    }

    const bool is_bid = side == Side::Buy;

    const std::size_t index = static_cast<std::size_t>(
        is_bid ? price - MIN_BID : price - MIN_ASK
    );

    PriceLevel& level = is_bid ? bids_[index] : asks_[index];

    if (!level.active) {
        level.active = true;

        if (is_bid) {
            if (best_bid_index_ < 0 ||
                static_cast<int>(index) > best_bid_index_) {
                best_bid_index_ = static_cast<int>(index);
            }
        } else {
            if (best_ask_index_ >= static_cast<int>(LEVEL_COUNT) ||
                static_cast<int>(index) < best_ask_index_) {
                best_ask_index_ = static_cast<int>(index);
            }
        }
    }

    level.fifo.push_back(id);
    auto fifo_it = std::prev(level.fifo.end());

    level.total_qty += qty;

    active_orders_.emplace(
        id, ActiveOrder{side, price, qty, index, fifo_it}
    );
    used_ids_.insert(id);

    return true;
}

bool ArrayOrderBook::cancel_order(OrderId id) {
    auto order_it = active_orders_.find(id);

    if (order_it == active_orders_.end()) {
        return false;
    }

    const ActiveOrder order = order_it->second;

    PriceLevel& level =
        order.side == Side::Buy
            ? bids_[order.index]
            : asks_[order.index];

    level.fifo.erase(order.fifo_it);
    level.total_qty -= order.qty;

    active_orders_.erase(order_it);

    if (level.fifo.empty()) {
        level.total_qty = 0;
        level.active = false;

        if (order.side == Side::Buy &&
            static_cast<int>(order.index) == best_bid_index_) {
            update_best_bid();
        }

        if (order.side == Side::Sell &&
            static_cast<int>(order.index) == best_ask_index_) {
            update_best_ask();
        }
    }

    return true;
}

void ArrayOrderBook::update_best_bid() {
    best_bid_index_ = -1;

    for (int i = static_cast<int>(LEVEL_COUNT) - 1; i >= 0; --i) {
        if (bids_[static_cast<std::size_t>(i)].active) {
            best_bid_index_ = i;
            return;
        }
    }
}

void ArrayOrderBook::update_best_ask() {
    best_ask_index_ = static_cast<int>(LEVEL_COUNT);

    for (int i = 0; i < static_cast<int>(LEVEL_COUNT); ++i) {
        if (asks_[static_cast<std::size_t>(i)].active) {
            best_ask_index_ = i;
            return;
        }
    }
}

BBO ArrayOrderBook::get_bbo() const {
    BBO result{};

    if (best_bid_index_ >= 0) {
        const std::size_t index =
            static_cast<std::size_t>(best_bid_index_);

        result.has_bid = true;
        result.bid_price = MIN_BID + static_cast<Price>(index);
        result.bid_qty = bids_[index].total_qty;
    }

    if (best_ask_index_ < static_cast<int>(LEVEL_COUNT)) {
        const std::size_t index =
            static_cast<std::size_t>(best_ask_index_);

        result.has_ask = true;
        result.ask_price = MIN_ASK + static_cast<Price>(index);
        result.ask_qty = asks_[index].total_qty;
    }

    return result;
}

std::vector<Level> ArrayOrderBook::get_bids(
    std::size_t depth
) const {
    std::vector<Level> result;
    result.reserve(std::min(depth, LEVEL_COUNT));

    for (int i = best_bid_index_;
         i >= 0 && result.size() < depth;
         --i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const PriceLevel& level = bids_[index];

        if (level.active) {
            result.push_back(Level{
                MIN_BID + static_cast<Price>(index),
                level.total_qty
            });
        }
    }

    return result;
}

std::vector<Level> ArrayOrderBook::get_asks(
    std::size_t depth
) const {
    std::vector<Level> result;
    result.reserve(std::min(depth, LEVEL_COUNT));

    for (int i = best_ask_index_;
         i < static_cast<int>(LEVEL_COUNT) &&
         result.size() < depth;
         ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const PriceLevel& level = asks_[index];

        if (level.active) {
            result.push_back(Level{
                MIN_ASK + static_cast<Price>(index),
                level.total_qty
            });
        }
    }

    return result;
}

}
