#include "OrderBookA.hpp"

void OrderBookA::add_order(uint64_t id, Side side, double price, long qty) {
    int64_t cents = price_to_cents(price);
    int tick = cents_to_tick(cents);
    orders_[id] = OrderRecord{id, side, price, qty, tick};

    if (side == Side::Buy) {
        bids_[cents] += qty;
    } else {
        asks_[cents] += qty;
    }
}

bool OrderBookA::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }

    const OrderRecord& ord = it->second;
    int64_t cents = price_to_cents(ord.price);

    if (ord.side == Side::Buy) {
        auto bit = bids_.find(cents);
        if (bit != bids_.end()) {
            bit->second -= ord.qty;
            if (bit->second <= 0) {
                bids_.erase(bit);
            }
        }
    } else {
        auto ait = asks_.find(cents);
        if (ait != asks_.end()) {
            ait->second -= ord.qty;
            if (ait->second <= 0) {
                asks_.erase(ait);
            }
        }
    }

    orders_.erase(it);
    return true;
}

BBO OrderBookA::get_bbo() const {
    BBO bbo;
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = cents_to_price(bids_.begin()->first);
        bbo.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = cents_to_price(asks_.begin()->first);
        bbo.ask_qty = asks_.begin()->second;
    }
    return bbo;
}

std::vector<Level> OrderBookA::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min<size_t>(depth, bids_.size()));

    int count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < depth; ++it, ++count) {
        result.push_back(Level{cents_to_price(it->first), it->second});
    }
    return result;
}

std::vector<Level> OrderBookA::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min<size_t>(depth, asks_.size()));

    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
        result.push_back(Level{cents_to_price(it->first), it->second});
    }
    return result;
}
