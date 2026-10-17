#include "map_order_book.hpp"

#include <limits>
#include <stdexcept>

void MapOrderBook::add_order(unsigned long long id, Side side, int price, long quantity) {
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
    if (orders.find(id) != orders.end()) {
        throw std::invalid_argument("This order ID is already live");
    }

    std::map<int, long>& levels = (side == Side::Buy) ? bids : asks;
    auto level = levels.find(price);
    long previousQuantity = 0;
    if (level != levels.end()) {
        previousQuantity = level->second;
    }
    if (quantity > std::numeric_limits<long>::max() - previousQuantity) {
        throw std::overflow_error("Total quantity would be too large");
    }

    Order next{};
    next.id = id;
    next.side = side;
    next.price = price;
    next.quantity = quantity;
    orders.emplace(id, next);
    try {
        levels[price] = previousQuantity + quantity;
    } catch (...) {
        orders.erase(id);
        throw;
    }
}

bool MapOrderBook::cancel_order(unsigned long long id) {
    auto found = orders.find(id);
    if (found == orders.end()) {
        return false;
    }
    const Order& current = found->second;
    std::map<int, long>& levels = (current.side == Side::Buy) ? bids : asks;
    auto level = levels.find(current.price);
    level->second -= current.quantity;
    if (level->second == 0) {
        levels.erase(level);
    }
    orders.erase(found);
    return true;
}

BBO MapOrderBook::get_bbo() const {
    BBO result{};
    if (!bids.empty()) {
        result.has_bid = true;
        result.bid_price = bids.rbegin()->first;
        result.bid_qty = bids.rbegin()->second;
    }
    if (!asks.empty()) {
        result.has_ask = true;
        result.ask_price = asks.begin()->first;
        result.ask_qty = asks.begin()->second;
    }
    return result;
}

std::vector<Level> MapOrderBook::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
        result.push_back(Level{it->first, it->second});
        if (result.size() == static_cast<std::size_t>(depth)) {
            break;
        }
    }
    return result;
}

std::vector<Level> MapOrderBook::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    for (auto it = asks.begin(); it != asks.end(); ++it) {
        result.push_back(Level{it->first, it->second});
        if (result.size() == static_cast<std::size_t>(depth)) {
            break;
        }
    }
    return result;
}
