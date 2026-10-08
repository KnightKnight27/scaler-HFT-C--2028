#include "OrderBookB.hpp"
#include <cmath>

OrderBookB::OrderBookB() {
    bids_.reserve(256);
    asks_.reserve(256);
}

void OrderBookB::add_order(uint64_t id, Side side, double price, long qty) {
    int64_t target_cents = price_to_cents(price);
    double normalized_price = cents_to_price(target_cents);
    int tick = cents_to_tick(target_cents);

    orders_[id] = OrderRecord{id, side, normalized_price, qty, tick};

    if (side == Side::Buy) {
        // Bids sorted descending: 99.99 down to 50.00
        auto it = std::lower_bound(bids_.begin(), bids_.end(), target_cents,
            [](const Level& lvl, int64_t cents) {
                return price_to_cents(lvl.price) > cents;
            });

        if (it != bids_.end() && price_to_cents(it->price) == target_cents) {
            it->qty += qty;
        } else {
            bids_.insert(it, Level{normalized_price, qty});
        }
    } else {
        // Asks sorted ascending: 100.01 up to 150.00
        auto it = std::lower_bound(asks_.begin(), asks_.end(), target_cents,
            [](const Level& lvl, int64_t cents) {
                return price_to_cents(lvl.price) < cents;
            });

        if (it != asks_.end() && price_to_cents(it->price) == target_cents) {
            it->qty += qty;
        } else {
            asks_.insert(it, Level{normalized_price, qty});
        }
    }
}

bool OrderBookB::cancel_order(uint64_t id) {
    auto oit = orders_.find(id);
    if (oit == orders_.end()) {
        return false;
    }

    const OrderRecord& ord = oit->second;
    int64_t target_cents = price_to_cents(ord.price);

    if (ord.side == Side::Buy) {
        auto it = std::lower_bound(bids_.begin(), bids_.end(), target_cents,
            [](const Level& lvl, int64_t cents) {
                return price_to_cents(lvl.price) > cents;
            });

        if (it != bids_.end() && price_to_cents(it->price) == target_cents) {
            it->qty -= ord.qty;
            if (it->qty <= 0) {
                bids_.erase(it);
            }
        }
    } else {
        auto it = std::lower_bound(asks_.begin(), asks_.end(), target_cents,
            [](const Level& lvl, int64_t cents) {
                return price_to_cents(lvl.price) < cents;
            });

        if (it != asks_.end() && price_to_cents(it->price) == target_cents) {
            it->qty -= ord.qty;
            if (it->qty <= 0) {
                asks_.erase(it);
            }
        }
    }

    orders_.erase(oit);
    return true;
}

BBO OrderBookB::get_bbo() const {
    BBO bbo;
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = bids_.front().price;
        bbo.bid_qty = bids_.front().qty;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = asks_.front().price;
        bbo.ask_qty = asks_.front().qty;
    }
    return bbo;
}

std::vector<Level> OrderBookB::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || bids_.empty()) return result;
    size_t count = std::min<size_t>(depth, bids_.size());
    result.assign(bids_.begin(), bids_.begin() + count);
    return result;
}

std::vector<Level> OrderBookB::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || asks_.empty()) return result;
    size_t count = std::min<size_t>(depth, asks_.size());
    result.assign(asks_.begin(), asks_.begin() + count);
    return result;
}
