#include "order_book.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

static bool order_before_id(const Order& order, unsigned long long id) {
    return order.id < id;
}

static bool level_before_price(const Level& level, int price) {
    return level.price < price;
}

void OrderBook::add_order(unsigned long long id, Side side, int price, long quantity) {
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("Side must be Buy or Sell");
    }
    if (quantity <= 0) {
        throw std::invalid_argument("Quantity must be positive");
    }
    if (side == Side::Buy && (price < 5000 || price > 9999)) {
        throw std::invalid_argument("Buy price must be between 5000 and 9999 ticks");
    }
    if (side == Side::Sell && (price < 10001 || price > 15000)) {
        throw std::invalid_argument("Sell price must be between 10001 and 15000 ticks");
    }

    auto position = std::lower_bound(orders.begin(), orders.end(), id, order_before_id);
    if (position != orders.end() && position->id == id) {
        throw std::invalid_argument("This order ID has already been used");
    }
    std::vector<Level>& levels = (side == Side::Buy) ? bids : asks;
    auto level = std::lower_bound(levels.begin(), levels.end(), price, level_before_price);
    bool existingPrice = level != levels.end() && level->price == price;
    long previousQuantity = existingPrice ? level->qty : 0;
    if (quantity > std::numeric_limits<long>::max() - previousQuantity) {
        throw std::overflow_error("Total quantity would be too large");
    }

    Order next{};
    next.id = id;
    next.side = side;
    next.price = price;
    next.quantity = quantity;
    auto inserted = orders.insert(position, next);
    try {
        if (existingPrice) {
            level->qty += quantity;
        } else {
            levels.insert(level, Level{price, quantity});
        }
    } catch (...) {
        orders.erase(inserted);
        throw;
    }
}

bool OrderBook::cancel_order(unsigned long long id) {
    auto found = std::lower_bound(orders.begin(), orders.end(), id, order_before_id);
    if (found == orders.end() || found->id != id || found->quantity == 0) {
        return false;
    }
    std::vector<Level>& levels = (found->side == Side::Buy) ? bids : asks;
    auto level = std::lower_bound(levels.begin(), levels.end(), found->price, level_before_price);
    level->qty -= found->quantity;
    if (level->qty == 0) {
        levels.erase(level);
    }
    found->quantity = 0;
    return true;
}

BBO OrderBook::get_bbo() const {
    BBO result{};
    if (!bids.empty()) {
        result.has_bid = true;
        result.bid_price = bids.back().price;
        result.bid_qty = bids.back().qty;
    }
    if (!asks.empty()) {
        result.has_ask = true;
        result.ask_price = asks.front().price;
        result.ask_qty = asks.front().qty;
    }
    return result;
}

std::vector<Level> OrderBook::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
        result.push_back(*it);
        if (result.size() == static_cast<std::size_t>(depth)) {
            break;
        }
    }
    return result;
}

std::vector<Level> OrderBook::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    for (const Level& level : asks) {
        result.push_back(level);
        if (result.size() == static_cast<std::size_t>(depth)) {
            break;
        }
    }
    return result;
}
