#pragma once

#include "common.h"
#include <unordered_map>
#include <vector>
#include <algorithm>

class OrderBookB {
public:
    OrderBookB() : best_bid_(MIN_BID_TICK - 1), best_ask_(MAX_ASK_TICK + 1) {}

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = price_to_tick(price);
        orders_[id] = Order{id, side, tick, qty};

        if (side == Side::Buy) {
            bids_[tick] += qty;
            if (tick > best_bid_) {
                best_bid_ = tick;
            }
        } else {
            asks_[tick] += qty;
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
        if (order.side == Side::Buy) {
            auto pit = bids_.find(order.tick);
            pit->second -= order.qty;
            if (pit->second <= 0) {
                bids_.erase(pit);
                if (order.tick == best_bid_) {
                    best_bid_ = MIN_BID_TICK - 1;
                    for (const auto& [t, q] : bids_) {
                        if (t > best_bid_) {
                            best_bid_ = t;
                        }
                    }
                }
            }
        } else {
            auto pit = asks_.find(order.tick);
            pit->second -= order.qty;
            if (pit->second <= 0) {
                asks_.erase(pit);
                if (order.tick == best_ask_) {
                    best_ask_ = MAX_ASK_TICK + 1;
                    for (const auto& [t, q] : asks_) {
                        if (t < best_ask_) {
                            best_ask_ = t;
                        }
                    }
                }
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
            bbo.bid_qty = bids_.at(best_bid_);
        }
        if (best_ask_ <= MAX_ASK_TICK) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(best_ask_);
            bbo.ask_qty = asks_.at(best_ask_);
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<int> ticks;
        ticks.reserve(bids_.size());
        for (const auto& [t, q] : bids_) {
            ticks.push_back(t);
        }
        std::sort(ticks.begin(), ticks.end(), std::greater<int>());

        std::vector<Level> result;
        int n = std::min(depth, static_cast<int>(ticks.size()));
        result.reserve(n);
        for (int i = 0; i < n; ++i) {
            result.push_back({tick_to_price(ticks[i]), bids_.at(ticks[i])});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<int> ticks;
        ticks.reserve(asks_.size());
        for (const auto& [t, q] : asks_) {
            ticks.push_back(t);
        }
        std::sort(ticks.begin(), ticks.end());

        std::vector<Level> result;
        int n = std::min(depth, static_cast<int>(ticks.size()));
        result.reserve(n);
        for (int i = 0; i < n; ++i) {
            result.push_back({tick_to_price(ticks[i]), asks_.at(ticks[i])});
        }
        return result;
    }

private:
    std::unordered_map<int, long> bids_;
    std::unordered_map<int, long> asks_;
    std::unordered_map<uint64_t, Order> orders_;
    int best_bid_;
    int best_ask_;
};
