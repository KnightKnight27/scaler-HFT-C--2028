#pragma once

#include "order_book.hpp"
#include <map>
#include <unordered_map>
#include <cmath>

// Implementation A: Using std::map (Red-Black Tree)
// - Bids sorted descending (highest price first)
// - Asks sorted ascending (lowest price first)
// - Order lookup via unordered_map for cancellation

class OrderBookMap : public OrderBook {
private:
    struct Order {
        Side side;
        double price;
        long qty;
    };

    // Bids sorted highest to lowest, Asks lowest to highest
    std::map<double, long, std::greater<double>> bids;
    std::map<double, long> asks;

    // Map order ID -> order details
    std::unordered_map<uint64_t, Order> orders;

    // Quick round helper for tick precision
    double round_price(double p) const {
        return std::round(p * 100.0) / 100.0;
    }

public:
    OrderBookMap() = default;

    void add_order(uint64_t id, Side side, double price, long qty) override {
        price = round_price(price);
        orders[id] = {side, price, qty};

        if (side == Side::Buy) {
            bids[price] += qty;
        } else {
            asks[price] += qty;
        }
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders.find(id);
        if (it == orders.end()) {
            return false;
        }

        Order order = it->second;
        if (order.side == Side::Buy) {
            auto bit = bids.find(order.price);
            if (bit != bids.end()) {
                bit->second -= order.qty;
                if (bit->second <= 0) {
                    bids.erase(bit);
                }
            }
        } else {
            auto ait = asks.find(order.price);
            if (ait != asks.end()) {
                ait->second -= order.qty;
                if (ait->second <= 0) {
                    asks.erase(ait);
                }
            }
        }

        orders.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo{false, 0.0, 0, false, 0.0, 0};

        if (!bids.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = bids.begin()->first;
            bbo.bid_qty = bids.begin()->second;
        }

        if (!asks.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = asks.begin()->first;
            bbo.ask_qty = asks.begin()->second;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        int count = 0;
        for (const auto& pair : bids) {
            if (count >= depth) break;
            result.push_back({pair.first, pair.second});
            count++;
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        int count = 0;
        for (const auto& pair : asks) {
            if (count >= depth) break;
            result.push_back({pair.first, pair.second});
            count++;
        }
        return result;
    }
};
