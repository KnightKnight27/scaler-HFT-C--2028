#pragma once

#include "order_book.hpp"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

// Implementation B: Using sorted std::vector
// - Contiguous memory layout
// - Bids sorted descending (highest first)
// - Asks sorted ascending (lowest first)
// - Binary search to locate price levels

class OrderBookVector : public OrderBook {
private:
    struct Order {
        Side side;
        double price;
        long qty;
    };

    std::vector<Level> bids;
    std::vector<Level> asks;
    std::unordered_map<uint64_t, Order> orders;

    double round_price(double p) const {
        return std::round(p * 100.0) / 100.0;
    }

public:
    OrderBookVector() = default;

    void add_order(uint64_t id, Side side, double price, long qty) override {
        price = round_price(price);
        orders[id] = {side, price, qty};

        if (side == Side::Buy) {
            // bids are sorted high to low
            auto it = std::lower_bound(bids.begin(), bids.end(), price,
                [](const Level& lvl, double p) { return lvl.price > p; });

            if (it != bids.end() && std::abs(it->price - price) < 1e-5) {
                it->qty += qty;
            } else {
                bids.insert(it, {price, qty});
            }
        } else {
            // asks are sorted low to high
            auto it = std::lower_bound(asks.begin(), asks.end(), price,
                [](const Level& lvl, double p) { return lvl.price < p; });

            if (it != asks.end() && std::abs(it->price - price) < 1e-5) {
                it->qty += qty;
            } else {
                asks.insert(it, {price, qty});
            }
        }
    }

    bool cancel_order(uint64_t id) override {
        auto oit = orders.find(id);
        if (oit == orders.end()) {
            return false;
        }

        Order order = oit->second;
        if (order.side == Side::Buy) {
            auto it = std::lower_bound(bids.begin(), bids.end(), order.price,
                [](const Level& lvl, double p) { return lvl.price > p; });

            if (it != bids.end() && std::abs(it->price - order.price) < 1e-5) {
                it->qty -= order.qty;
                if (it->qty <= 0) {
                    bids.erase(it);
                }
            }
        } else {
            auto it = std::lower_bound(asks.begin(), asks.end(), order.price,
                [](const Level& lvl, double p) { return lvl.price < p; });

            if (it != asks.end() && std::abs(it->price - order.price) < 1e-5) {
                it->qty -= order.qty;
                if (it->qty <= 0) {
                    asks.erase(it);
                }
            }
        }

        orders.erase(oit);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo{false, 0.0, 0, false, 0.0, 0};

        if (!bids.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids[0].price;
            bbo.bid_qty = bids[0].qty;
        }

        if (!asks.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks[0].price;
            bbo.ask_qty = asks[0].qty;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        int count = std::min((int)bids.size(), depth);
        for (int i = 0; i < count; i++) {
            result.push_back(bids[i]);
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        int count = std::min((int)asks.size(), depth);
        for (int i = 0; i < count; i++) {
            result.push_back(asks[i]);
        }
        return result;
    }
};
