#include "OrderBookMap.hpp"
#include <algorithm>

void OrderBookMap::add_order(uint64_t id, Side side, double price, long qty) {
    PriceTick tick = price_to_ticks(price);
    orders_[id] = {side, tick, qty};

    if (side == Side::Buy) {
        bids_[tick] += qty;
    } else {
        asks_[tick] += qty;
    }
}

bool OrderBookMap::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }

    const auto& rec = it->second;
    if (rec.side == Side::Buy) {
        auto bit = bids_.find(rec.tick);
        if (bit != bids_.end()) {
            bit->second -= rec.qty;
            if (bit->second <= 0) {
                bids_.erase(bit);
            }
        }
    } else {
        auto ait = asks_.find(rec.tick);
        if (ait != asks_.end()) {
            ait->second -= rec.qty;
            if (ait->second <= 0) {
                asks_.erase(ait);
            }
        }
    }

    orders_.erase(it);
    return true;
}

BBO OrderBookMap::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = ticks_to_price(bids_.begin()->first);
        bbo.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = ticks_to_price(asks_.begin()->first);
        bbo.ask_qty = asks_.begin()->second;
    }
    return bbo;
}

std::vector<Level> OrderBookMap::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min((size_t)depth, bids_.size()));
    int count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < depth; ++it, ++count) {
        result.push_back({ticks_to_price(it->first), it->second});
    }
    return result;
}

std::vector<Level> OrderBookMap::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    result.reserve(std::min((size_t)depth, asks_.size()));
    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth; ++it, ++count) {
        result.push_back({ticks_to_price(it->first), it->second});
    }
    return result;
}
