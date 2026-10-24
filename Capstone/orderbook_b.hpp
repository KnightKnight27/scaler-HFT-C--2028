#pragma once

#include "utils.hpp"
#include <algorithm>
#include <vector>

class OrderBookB {
private:
    using Tick = int32_t;

    struct TickLevel {
        Tick tick;
        long qty;
    };

    struct OrderEntry {
        uint64_t id;
        Side side;
        Tick tick;
        long qty;
        bool active;
    };

    // Bids sorted ASCENDING: bids.back() is the highest bid (Best Bid)
    std::vector<TickLevel> bids;

    // Asks sorted DESCENDING: asks.back() is the lowest ask (Best Ask)
    std::vector<TickLevel> asks;

    // Contiguous vector of orders sorted by ID for binary search
    std::vector<OrderEntry> orders;

public:
    OrderBookB() {
        bids.reserve(TICKS_PER_SIDE);
        asks.reserve(TICKS_PER_SIDE);
        orders.reserve(750'000);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        Tick tick = price_to_tick(price);

        if (side == Side::Buy) {
            auto it = std::lower_bound(
                bids.begin(), bids.end(), tick,
                [](const TickLevel& lvl, Tick t) { return lvl.tick < t; }
            );
            if (it != bids.end() && it->tick == tick) {
                it->qty += qty;
            } else {
                bids.insert(it, {tick, qty});
            }
        } else {
            auto it = std::lower_bound(
                asks.begin(), asks.end(), tick,
                [](const TickLevel& lvl, Tick t) { return lvl.tick > t; }
            );
            if (it != asks.end() && it->tick == tick) {
                it->qty += qty;
            } else {
                asks.insert(it, {tick, qty});
            }
        }

        if (orders.empty() || id > orders.back().id) {
            orders.push_back({id, side, tick, qty, true});
        } else {
            auto it = std::lower_bound(
                orders.begin(), orders.end(), id,
                [](const OrderEntry& o, uint64_t target_id) { return o.id < target_id; }
            );
            orders.insert(it, {id, side, tick, qty, true});
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = std::lower_bound(
            orders.begin(), orders.end(), id,
            [](const OrderEntry& o, uint64_t target_id) { return o.id < target_id; }
        );

        if (it == orders.end() || it->id != id || !it->active) {
            return false;
        }

        it->active = false;

        if (it->side == Side::Buy) {
            auto lvl_it = std::lower_bound(
                bids.begin(), bids.end(), it->tick,
                [](const TickLevel& lvl, Tick t) { return lvl.tick < t; }
            );
            if (lvl_it != bids.end() && lvl_it->tick == it->tick) {
                lvl_it->qty -= it->qty;
                if (lvl_it->qty <= 0) {
                    bids.erase(lvl_it);
                }
            }
        } else {
            auto lvl_it = std::lower_bound(
                asks.begin(), asks.end(), it->tick,
                [](const TickLevel& lvl, Tick t) { return lvl.tick > t; }
            );
            if (lvl_it != asks.end() && lvl_it->tick == it->tick) {
                lvl_it->qty -= it->qty;
                if (lvl_it->qty <= 0) {
                    asks.erase(lvl_it);
                }
            }
        }

        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        bbo.has_bid = false;
        bbo.has_ask = false;

        if (!bids.empty()) {
            const auto& best_bid = bids.back();
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(best_bid.tick);
            bbo.bid_qty = best_bid.qty;
        }

        if (!asks.empty()) {
            const auto& best_ask = asks.back();
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(best_ask.tick);
            bbo.ask_qty = best_ask.qty;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        for (auto it = bids.rbegin(); it != bids.rend() && depth > 0; ++it, --depth) {
            result.push_back({tick_to_price(it->tick), it->qty});
        }

        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        for (auto it = asks.rbegin(); it != asks.rend() && depth > 0; ++it, --depth) {
            result.push_back({tick_to_price(it->tick), it->qty});
        }

        return result;
    }
};