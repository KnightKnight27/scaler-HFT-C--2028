#include "orderbook_a.hpp"

OrderBookA::OrderBookA() {
    orders_.reserve(1050000);
}

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty) {
    if (qty <= 0) return;
    const int32_t tick = price_to_tick(price);
    orders_.emplace(id, OrderInfo{side, tick, qty});

    if (side == Side::Buy) {
        bids_[tick] += qty;
    } else {
        asks_[tick] += qty;
    }
}

bool OrderBookA::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }

    const OrderInfo info = it->second;
    orders_.erase(it);

    if (info.side == Side::Buy) {
        auto bid_it = bids_.find(info.tick);
        if (bid_it != bids_.end()) {
            bid_it->second -= info.qty;
            if (bid_it->second <= 0) {
                bids_.erase(bid_it);
            }
        }
    } else {
        auto ask_it = asks_.find(info.tick);
        if (ask_it != asks_.end()) {
            ask_it->second -= info.qty;
            if (ask_it->second <= 0) {
                asks_.erase(ask_it);
            }
        }
    }
    return true;
}

BBO OrderBookA::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (!bids_.empty()) {
        const auto& best_bid = *bids_.begin();
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(best_bid.first);
        bbo.bid_qty = best_bid.second;
    }
    if (!asks_.empty()) {
        const auto& best_ask = *asks_.begin();
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(best_ask.first);
        bbo.ask_qty = best_ask.second;
    }
    return bbo;
}

std::vector<Level> OrderBookA::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(static_cast<size_t>(depth));

    int count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < depth; ++it, ++count) {
        result.push_back(Level{tick_to_price(it->first), it->second});
    }
    return result;
}

std::vector<Level> OrderBookA::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(static_cast<size_t>(depth));

    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
        result.push_back(Level{tick_to_price(it->first), it->second});
    }
    return result;
}