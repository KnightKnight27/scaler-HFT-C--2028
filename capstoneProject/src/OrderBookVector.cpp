#include "OrderBookVector.hpp"
#include <algorithm>

void OrderBookVector::add_order(uint64_t id, Side side, double price, long qty) {
    PriceTick tick = price_to_ticks(price);
    orders_[id] = {side, tick, qty};

    if (side == Side::Buy) {
        auto it = std::lower_bound(bids_.begin(), bids_.end(), tick,
            [](const LevelTick& lvl, PriceTick val) {
                return lvl.tick > val; // descending order
            });
        if (it != bids_.end() && it->tick == tick) {
            it->qty += qty;
        } else {
            bids_.insert(it, LevelTick{tick, qty});
        }
    } else {
        auto it = std::lower_bound(asks_.begin(), asks_.end(), tick,
            [](const LevelTick& lvl, PriceTick val) {
                return lvl.tick < val; // ascending order
            });
        if (it != asks_.end() && it->tick == tick) {
            it->qty += qty;
        } else {
            asks_.insert(it, LevelTick{tick, qty});
        }
    }
}

bool OrderBookVector::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }

    const auto& rec = it->second;
    if (rec.side == Side::Buy) {
        auto bit = std::lower_bound(bids_.begin(), bids_.end(), rec.tick,
            [](const LevelTick& lvl, PriceTick val) {
                return lvl.tick > val;
            });
        if (bit != bids_.end() && bit->tick == rec.tick) {
            bit->qty -= rec.qty;
            if (bit->qty <= 0) {
                bids_.erase(bit);
            }
        }
    } else {
        auto ait = std::lower_bound(asks_.begin(), asks_.end(), rec.tick,
            [](const LevelTick& lvl, PriceTick val) {
                return lvl.tick < val;
            });
        if (ait != asks_.end() && ait->tick == rec.tick) {
            ait->qty -= rec.qty;
            if (ait->qty <= 0) {
                asks_.erase(ait);
            }
        }
    }

    orders_.erase(it);
    return true;
}

BBO OrderBookVector::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};
    if (!bids_.empty()) {
        bbo.has_bid = true;
        bbo.bid_price = ticks_to_price(bids_.front().tick);
        bbo.bid_qty = bids_.front().qty;
    }
    if (!asks_.empty()) {
        bbo.has_ask = true;
        bbo.ask_price = ticks_to_price(asks_.front().tick);
        bbo.ask_qty = asks_.front().qty;
    }
    return bbo;
}

std::vector<Level> OrderBookVector::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    int count = std::min(depth, (int)bids_.size());
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.push_back({ticks_to_price(bids_[i].tick), bids_[i].qty});
    }
    return result;
}

std::vector<Level> OrderBookVector::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) return result;
    int count = std::min(depth, (int)asks_.size());
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.push_back({ticks_to_price(asks_[i].tick), asks_[i].qty});
    }
    return result;
}
