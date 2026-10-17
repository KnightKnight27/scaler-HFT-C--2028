#pragma once

#include "common.h"
#include <array>
#include <unordered_map>
#include <vector>

class OrderBookC {
public:
    OrderBookC() : best_bid_(MIN_BID_TICK - 1), best_ask_(MAX_ASK_TICK + 1) {
        levels_.fill(0);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = Order{id, side, tick, qty};
        levels_[tick - MIN_TICK] += qty;

        if (side == Side::Buy) {
            if (tick > best_bid_) {
                best_bid_ = tick;
            }
        } else {
            if (tick < best_ask_) {
                best_ask_ = tick;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }

        const Order& order = it->second;
        int idx = order.tick - MIN_TICK;
        levels_[idx] -= order.qty;

        if (levels_[idx] <= 0) {
            levels_[idx] = 0;
            if (order.side == Side::Buy && order.tick == best_bid_) {
                int t = best_bid_ - 1;
                while (t >= MIN_BID_TICK && levels_[t - MIN_TICK] == 0) {
                    --t;
                }
                best_bid_ = t;
            } else if (order.side == Side::Sell && order.tick == best_ask_) {
                int t = best_ask_ + 1;
                while (t <= MAX_ASK_TICK && levels_[t - MIN_TICK] == 0) {
                    ++t;
                }
                best_ask_ = t;
            }
        }

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (best_bid_ >= MIN_BID_TICK) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(best_bid_);
            bbo.bid_qty = levels_[best_bid_ - MIN_TICK];
        }
        if (best_ask_ <= MAX_ASK_TICK) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(best_ask_);
            bbo.ask_qty = levels_[best_ask_ - MIN_TICK];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        result.reserve(depth);
        int count = 0;
        for (int t = best_bid_; t >= MIN_BID_TICK && count < depth; --t) {
            long q = levels_[t - MIN_TICK];
            if (q > 0) {
                result.push_back({tick_to_price(t), q});
                ++count;
            }
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        result.reserve(depth);
        int count = 0;
        for (int t = best_ask_; t <= MAX_ASK_TICK && count < depth; ++t) {
            long q = levels_[t - MIN_TICK];
            if (q > 0) {
                result.push_back({tick_to_price(t), q});
                ++count;
            }
        }
        return result;
    }

private:
    std::array<long, NUM_TICKS> levels_;
    std::unordered_map<uint64_t, Order> orders_;
    int best_bid_;
    int best_ask_;
};
