#include "order_book_c.hpp"

OrderBookC::OrderBookC() : price_quantities_(15001, 0), best_bid_tick_(-1), best_ask_tick_(999999) {}

void OrderBookC::add_order(uint64_t id, Side side, double price, long qty) {
    int price_tick = static_cast<int>(std::round(price * 100.0));

    orders_[id] = {side, price_tick, qty};
    price_quantities_[price_tick] += qty;
    if (side == Side::Buy) {
        if (best_bid_tick_ == -1 || price_tick > best_bid_tick_) {
            best_bid_tick_ = price_tick;
        }
    } else {
        if (best_ask_tick_ == 999999 || price_tick < best_ask_tick_) {
            best_ask_tick_ = price_tick;
        }
    }
}

bool OrderBookC::cancel_order(uint64_t id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        return false;
    }

    const auto& order = it->second;
    int price_tick = order.price_tick;
    long qty = order.qty;

    price_quantities_[price_tick] -= qty;
    if (price_quantities_[price_tick] < 0) {
        price_quantities_[price_tick] = 0;
    }

    if (price_quantities_[price_tick] == 0) {
        if (order.side == Side::Buy && price_tick == best_bid_tick_) {
            int new_best = -1;
            for (int t = price_tick - 1; t >= 5000; --t) {
                if (price_quantities_[t] > 0) {
                    new_best = t;
                    break;
                }
            }
            best_bid_tick_ = new_best;
        } else if (order.side == Side::Sell && price_tick == best_ask_tick_) {
            int new_best = 999999;
            for (int t = price_tick + 1; t <= 15000; ++t) {
                if (price_quantities_[t] > 0) {
                    new_best = t;
                    break;
                }
            }
            best_ask_tick_ = new_best;
        }
    }

    orders_.erase(it);
    return true;
}

BBO OrderBookC::get_bbo() const {
    BBO bbo{false, 0.0, 0, false, 0.0, 0};

    if (best_bid_tick_ != -1 && price_quantities_[best_bid_tick_] > 0) {
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(best_bid_tick_);
        bbo.bid_qty = price_quantities_[best_bid_tick_];
    }

    if (best_ask_tick_ != 999999 && price_quantities_[best_ask_tick_] > 0) {
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(best_ask_tick_);
        bbo.ask_qty = price_quantities_[best_ask_tick_];
    }

    return bbo;
}

std::vector<Level> OrderBookC::get_bids(int depth) const {
    std::vector<Level> levels;
    levels.reserve(depth);

    int count = 0;
    if (best_bid_tick_ != -1) {
        for (int t = best_bid_tick_; t >= 5000; --t) {
            if (price_quantities_[t] > 0) {
                levels.push_back({tick_to_price(t), price_quantities_[t]});
                count++;
                if (count >= depth) break;
            }
        }
    }
    return levels;
}

std::vector<Level> OrderBookC::get_asks(int depth) const {
    std::vector<Level> levels;
    levels.reserve(depth);

    int count = 0;
    if (best_ask_tick_ != 999999) {
        for (int t = best_ask_tick_; t <= 15000; ++t) {
            if (price_quantities_[t] > 0) {
                levels.push_back({tick_to_price(t), price_quantities_[t]});
                count++;
                if (count >= depth) break;
            }
        }
    }
    return levels;
}