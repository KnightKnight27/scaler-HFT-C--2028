#pragma once

// Implementation B: Sorted Contiguous Vector + std::unordered_map
// Stores active price levels in contiguous std::vector containers kept in sorted order.
// Bids are sorted descending (highest price at index 0).
// Asks are sorted ascending (lowest price at index 0).
// Uses std::lower_bound (binary search) to locate price levels.
// Uses std::unordered_map for fast order ID lookup during cancellation.

#include "common.hpp"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

class OrderBookVector {
private:
    struct LevelEntry {
        int64_t price_cents;
        long total_qty;
    };

    struct OrderData {
        Side side;
        int64_t price_cents;
        long qty;
    };

    // Fast order lookup by ID
    std::unordered_map<uint64_t, OrderData> order_map;

    // Contiguous sorted vectors of active price levels
    std::vector<LevelEntry> bid_levels; // Sorted descending
    std::vector<LevelEntry> ask_levels; // Sorted ascending

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int64_t price_cents = to_cents(price);
        order_map[id] = OrderData{side, price_cents, qty};

        if (side == Side::Buy) {
            // Binary search descending: look for first element <= price_cents
            auto it = std::lower_bound(bid_levels.begin(), bid_levels.end(), price_cents,
                [](const LevelEntry& lvl, int64_t p) {
                    return lvl.price_cents > p;
                });

            if (it != bid_levels.end() && it->price_cents == price_cents) {
                it->total_qty += qty;
            } else {
                bid_levels.insert(it, LevelEntry{price_cents, qty});
            }
        } else {
            // Binary search ascending: look for first element >= price_cents
            auto it = std::lower_bound(ask_levels.begin(), ask_levels.end(), price_cents,
                [](const LevelEntry& lvl, int64_t p) {
                    return lvl.price_cents < p;
                });

            if (it != ask_levels.end() && it->price_cents == price_cents) {
                it->total_qty += qty;
            } else {
                ask_levels.insert(it, LevelEntry{price_cents, qty});
            }
        }
    }

    bool cancel_order(uint64_t id) {
        auto map_it = order_map.find(id);
        if (map_it == order_map.end()) {
            return false; // Order not found or already cancelled
        }

        const auto& info = map_it->second;
        if (info.side == Side::Buy) {
            auto it = std::lower_bound(bid_levels.begin(), bid_levels.end(), info.price_cents,
                [](const LevelEntry& lvl, int64_t p) {
                    return lvl.price_cents > p;
                });

            if (it != bid_levels.end() && it->price_cents == info.price_cents) {
                it->total_qty -= info.qty;
                if (it->total_qty <= 0) {
                    bid_levels.erase(it);
                }
            }
        } else {
            auto it = std::lower_bound(ask_levels.begin(), ask_levels.end(), info.price_cents,
                [](const LevelEntry& lvl, int64_t p) {
                    return lvl.price_cents < p;
                });

            if (it != ask_levels.end() && it->price_cents == info.price_cents) {
                it->total_qty -= info.qty;
                if (it->total_qty <= 0) {
                    ask_levels.erase(it);
                }
            }
        }

        order_map.erase(map_it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};

        if (!bid_levels.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = to_double(bid_levels[0].price_cents);
            bbo.bid_qty = bid_levels[0].total_qty;
        } else {
            bbo.has_bid = false;
            bbo.bid_price = 0.0;
            bbo.bid_qty = 0;
        }

        if (!ask_levels.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = to_double(ask_levels[0].price_cents);
            bbo.ask_qty = ask_levels[0].total_qty;
        } else {
            bbo.has_ask = false;
            bbo.ask_price = 0.0;
            bbo.ask_qty = 0;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> levels;
        int limit = std::min(depth, static_cast<int>(bid_levels.size()));
        levels.reserve(limit);
        for (int i = 0; i < limit; ++i) {
            levels.push_back(Level{to_double(bid_levels[i].price_cents), bid_levels[i].total_qty});
        }
        return levels;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> levels;
        int limit = std::min(depth, static_cast<int>(ask_levels.size()));
        levels.reserve(limit);
        for (int i = 0; i < limit; ++i) {
            levels.push_back(Level{to_double(ask_levels[i].price_cents), ask_levels[i].total_qty});
        }
        return levels;
    }
};

using OrderBookB = OrderBookVector;
