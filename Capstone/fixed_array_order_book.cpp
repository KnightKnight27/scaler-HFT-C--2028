// Data structure: fixed price-tick indexed arrays with an std::unordered_map ID index.
#include "fixed_array_order_book.hpp"

#include <stdexcept>

namespace orderbook {

void FixedArrayOrderBook::add_order(std::uint64_t id, Side side,
                                    PriceTicks price_ticks, long qty) {
    validate_order(side, price_ticks, qty);
    const auto inserted = orders_.emplace(id, Order{side, price_ticks, qty});
    if (!inserted.second) {
        throw std::invalid_argument("an order with this ID has already been used");
    }

    if (side == Side::Buy) {
        bid_qty_[price_ticks] += qty;
        if (price_ticks > best_bid_ticks_) {
            best_bid_ticks_ = price_ticks;
        }
    } else {
        ask_qty_[price_ticks] += qty;
        if (best_ask_ticks_ == -1 || price_ticks < best_ask_ticks_) {
            best_ask_ticks_ = price_ticks;
        }
    }
}

bool FixedArrayOrderBook::cancel_order(std::uint64_t id) {
    const auto order = orders_.find(id);
    if (order == orders_.end() || !order->second.live) {
        return false;
    }
    const auto side = order->second.side;
    const auto price = order->second.price_ticks;
    if (side == Side::Buy) {
        bid_qty_[price] -= order->second.qty;
        if (bid_qty_[price] == 0 && price == best_bid_ticks_) {
            best_bid_ticks_ = -1;
            for (PriceTicks tick = price - 1; tick >= 5000; --tick) {
                if (bid_qty_[tick] != 0) {
                    best_bid_ticks_ = tick;
                    break;
                }
            }
        }
    } else {
        ask_qty_[price] -= order->second.qty;
        if (ask_qty_[price] == 0 && price == best_ask_ticks_) {
            best_ask_ticks_ = -1;
            for (PriceTicks tick = price + 1; tick <= 15000; ++tick) {
                if (ask_qty_[tick] != 0) {
                    best_ask_ticks_ = tick;
                    break;
                }
            }
        }
    }
    order->second.live = false;
    return true;
}

BBO FixedArrayOrderBook::get_bbo() const {
    BBO bbo;
    if (best_bid_ticks_ != -1) {
        bbo.has_bid = true;
        bbo.bid_price_ticks = best_bid_ticks_;
        bbo.bid_qty = bid_qty_[best_bid_ticks_];
    }
    if (best_ask_ticks_ != -1) {
        bbo.has_ask = true;
        bbo.ask_price_ticks = best_ask_ticks_;
        bbo.ask_qty = ask_qty_[best_ask_ticks_];
    }
    return bbo;
}

std::vector<Level> FixedArrayOrderBook::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    result.reserve(static_cast<std::size_t>(depth));
    for (PriceTicks tick = best_bid_ticks_;
         tick >= 5000 && result.size() < static_cast<std::size_t>(depth); --tick) {
        if (bid_qty_[tick] != 0) {
            result.push_back({tick, bid_qty_[tick]});
        }
    }
    return result;
}

std::vector<Level> FixedArrayOrderBook::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    result.reserve(static_cast<std::size_t>(depth));
    for (PriceTicks tick = best_ask_ticks_;
         tick != -1 && tick <= 15000 &&
         result.size() < static_cast<std::size_t>(depth);
         ++tick) {
        if (ask_qty_[tick] != 0) {
            result.push_back({tick, ask_qty_[tick]});
        }
    }
    return result;
}

}  // namespace orderbook
