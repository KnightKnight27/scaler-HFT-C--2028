#include "OrderBookB.hpp"
#include <algorithm>

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty) {
    price = normalize_price(price);
    orders_[id] = {side, price, qty};

    if (side == Side::Buy) {
        // bids_ sorted descending by price
        auto it = std::lower_bound(bids_.begin(), bids_.end(), price, [](const Level& lvl, double p) {
            return lvl.price > p;
        });
        if (it != bids_.end() && std::abs(it->price - price) < 1e-5) {
            it->qty += qty;
        } else {
            bids_.insert(it, Level{price, qty});
        }
    } else {
        // asks_ sorted ascending by price
        auto it = std::lower_bound(asks_.begin(), asks_.end(), price, [](const Level& lvl, double p) {
            return lvl.price < p;
        });
        if (it != asks_.end() && std::abs(it->price - price) < 1e-5) {
            it->qty += qty;
        } else {
            asks_.insert(it, Level{price, qty});
        }
    }
}

bool OrderBookB::cancel_order(uint64_t id) {
    auto oit = orders_.find(id);
    if (oit == orders_.end()) {
        return false;
    }

    const auto& order = oit->second;
    if (order.side == Side::Buy) {
        auto it = std::lower_bound(bids_.begin(), bids_.end(), order.price, [](const Level& lvl, double p) {
            return lvl.price > p;
        });
        if (it != bids_.end() && std::abs(it->price - order.price) < 1e-5) {
            it->qty -= order.qty;
            if (it->qty <= 0) {
                bids_.erase(it);
            }
        }
    } else {
        auto it = std::lower_bound(asks_.begin(), asks_.end(), order.price, [](const Level& lvl, double p) {
            return lvl.price < p;
        });
        if (it != asks_.end() && std::abs(it->price - order.price) < 1e-5) {
            it->qty -= order.qty;
            if (it->qty <= 0) {
                asks_.erase(it);
            }
        }
    }

    orders_.erase(oit);
    return true;
}

BBO OrderBookB::get_bbo() const {
    BBO bbo{};
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = bids_.front().price;
        bbo.bid_qty = bids_.front().qty;
    } else {
        bbo.has_bid = false;
        bbo.bid_price = 0.0;
        bbo.bid_qty = 0;
    }

    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = asks_.front().price;
        bbo.ask_qty = asks_.front().qty;
    } else {
        bbo.has_ask = false;
        bbo.ask_price = 0.0;
        bbo.ask_qty = 0;
    }
    return bbo;
}

std::vector<Level> OrderBookB::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    int count = std::min(depth, static_cast<int>(bids_.size()));
    result.assign(bids_.begin(), bids_.begin() + count);
    return result;
}

std::vector<Level> OrderBookB::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    int count = std::min(depth, static_cast<int>(asks_.size()));
    result.assign(asks_.begin(), asks_.begin() + count);
    return result;
}
