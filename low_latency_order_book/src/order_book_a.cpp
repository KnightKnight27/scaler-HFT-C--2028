#include "order_book_a.hpp"
#include <cmath>

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty) {
    int price_tick = static_cast<int>(std::round(price * 100.0));

    orders_[id] = {side, price_tick, qty};
    if (side == Side::Buy) {
        bids_[price_tick] += qty;
    } else {
        asks_[price_tick] += qty;
    }
}

bool OrderBookA::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }
    const auto& order = it->second;
    if (order.side == Side::Buy) {
        auto bid_it = bids_.find(order.price_tick);
        if (bid_it != bids_.end()) {
            bid_it->second -= order.qty;
            if (bid_it->second <= 0) {
                bids_.erase(bid_it);
            }
        }
    } else {
        auto ask_it = asks_.find(order.price_tick);
        if (ask_it != asks_.end()) {
            ask_it->second -= order.qty;
            if (ask_it->second <= 0) {
                asks_.erase(ask_it); 
            }
        }
    }

    orders_.erase(it);
    return true;
}

BBO OrderBookA::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(bids_.begin()->first);
        bbo.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(asks_.begin()->first);
        bbo.ask_qty = asks_.begin()->second;
    }
    return bbo;
}
std::vector<Level> OrderBookA::get_bids(int depth) const {
    std::vector<Level> levels;
    levels.reserve(depth);

    int count = 0;
    for (auto const& [price_tick, qty] : bids_) {
        if (count >= depth) break;
        levels.push_back({tick_to_price(price_tick), qty});
        count++;
    }
    return levels;
}
std::vector<Level> OrderBookA::get_asks(int depth) const {
    std::vector<Level> levels;
    levels.reserve(depth);

    int count = 0;
    for (auto const& [price_tick, qty] : asks_) {
        if (count >= depth) break;
        levels.push_back({tick_to_price(price_tick), qty});
        count++;
    }
    return levels;
}