#pragma once

#include "order_book.hpp"
#include <vector>
#include <unordered_map>
#include <cmath>

// Implementation C (My Design): Fixed Price-Indexed Array
//
// Idea:
// - Tick size is fixed at 0.01.
// - Bids are always [50.00, 99.99] -> exactly 5000 levels.
// - Asks are always [100.01, 150.00] -> exactly 5000 levels.
//
// Instead of searching in a tree or vector, we map the price directly to an array index:
// - Bid price index = (price - 50.00) * 100
// - Ask price index = (price - 100.01) * 100
//
// We track best_bid and best_ask index so get_bbo() is instant O(1).
// Adding an order is instant O(1) array access.

class OrderBookIndexed : public OrderBook {
private:
    struct Order {
        Side side;
        int idx;
        long qty;
    };

    // 5000 levels each for bids and asks
    long bid_levels[5000] = {0};
    long ask_levels[5000] = {0};

    // Track the current best index (-1 if empty)
    int best_bid = -1;
    int best_ask = -1;

    // Track live orders for O(1) cancel lookup
    std::unordered_map<uint64_t, Order> orders;

    // Round helper to avoid double precision issues
    double round_price(double p) const {
        return std::round(p * 100.0) / 100.0;
    }

    // Convert price to array index
    int bid_to_idx(double price) const {
        return (int)std::round((price - 50.00) * 100.0);
    }

    int ask_to_idx(double price) const {
        return (int)std::round((price - 100.01) * 100.0);
    }

    // Convert index back to price
    double idx_to_bid(int idx) const {
        return round_price(50.00 + idx * 0.01);
    }

    double idx_to_ask(int idx) const {
        return round_price(100.01 + idx * 0.01);
    }

public:
    OrderBookIndexed() = default;

    void add_order(uint64_t id, Side side, double price, long qty) override {
        if (side == Side::Buy) {
            int idx = bid_to_idx(price);
            if (idx >= 0 && idx < 5000) {
                bid_levels[idx] += qty;
                orders[id] = {side, idx, qty};

                // Higher index = higher price = better bid
                if (idx > best_bid) {
                    best_bid = idx;
                }
            }
        } else {
            int idx = ask_to_idx(price);
            if (idx >= 0 && idx < 5000) {
                ask_levels[idx] += qty;
                orders[id] = {side, idx, qty};

                // Lower index = lower price = better ask
                if (best_ask == -1 || idx < best_ask) {
                    best_ask = idx;
                }
            }
        }
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders.find(id);
        if (it == orders.end()) {
            return false;
        }

        Order order = it->second;
        if (order.side == Side::Buy) {
            int idx = order.idx;
            bid_levels[idx] -= order.qty;

            // If the level quantity becomes 0
            if (bid_levels[idx] <= 0) {
                bid_levels[idx] = 0;
                // If we removed the best bid, find the next one below it
                if (idx == best_bid) {
                    while (best_bid >= 0 && bid_levels[best_bid] == 0) {
                        best_bid--;
                    }
                }
            }
        } else {
            int idx = order.idx;
            ask_levels[idx] -= order.qty;

            // If the level quantity becomes 0
            if (ask_levels[idx] <= 0) {
                ask_levels[idx] = 0;
                // If we removed the best ask, find the next one above it
                if (idx == best_ask) {
                    while (best_ask < 5000 && ask_levels[best_ask] == 0) {
                        best_ask++;
                    }
                    if (best_ask >= 5000) {
                        best_ask = -1;
                    }
                }
            }
        }

        orders.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo{false, 0.0, 0, false, 0.0, 0};

        if (best_bid >= 0) {
            bbo.has_bid = true;
            bbo.bid_price = idx_to_bid(best_bid);
            bbo.bid_qty = bid_levels[best_bid];
        }

        if (best_ask >= 0) {
            bbo.has_ask = true;
            bbo.ask_price = idx_to_ask(best_ask);
            bbo.ask_qty = ask_levels[best_ask];
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        if (best_bid < 0) return result;

        // Walk down from best bid
        for (int i = best_bid; i >= 0 && (int)result.size() < depth; i--) {
            if (bid_levels[i] > 0) {
                result.push_back({idx_to_bid(i), bid_levels[i]});
            }
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        if (best_ask < 0) return result;

        // Walk up from best ask
        for (int i = best_ask; i < 5000 && (int)result.size() < depth; i++) {
            if (ask_levels[i] > 0) {
                result.push_back({idx_to_ask(i), ask_levels[i]});
            }
        }
        return result;
    }
};
