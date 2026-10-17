#pragma once

// Implementation C: Custom Design - Direct Price-Indexed Array with BBO Pointers
// Takes advantage of the fixed price range [50.00, 150.00] with 0.01 tick size (10,001 slots).
// Price levels are directly indexed in a flat array (O(1) direct memory access, no hashing, no tree search).
// The entire array (~80KB) easily fits in the CPU L2 cache for maximum cache locality.
// Best bid and best ask pointers are cached for O(1) BBO queries.
// On cancel of the top level, scans to the next active price.
// Order lookup for cancellation uses std::unordered_map.

#include "common.hpp"
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>
#include <cstdint>

class OrderBookArray {
private:
    static constexpr int MIN_CENTS = Config::MIN_CENTS; // 5000
    static constexpr int MAX_CENTS = Config::MAX_CENTS; // 15000
    static constexpr int NUM_LEVELS = Config::TOTAL_TICKS; // 10001

    struct OrderData {
        Side side;
        int price_cents;
        long qty;
    };

    // Flat array of quantities per price level. Index = price_cents - MIN_CENTS
    std::array<long, NUM_LEVELS> price_qty{};

    // Fast order ID lookup for cancellation
    std::unordered_map<uint64_t, OrderData> order_map;

    // Cached best bid and ask price in cents (-1 means no bid/ask exists)
    int best_bid_cents = -1;
    int best_ask_cents = -1;

public:
    OrderBookArray() {
        price_qty.fill(0);
    }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int cents = static_cast<int>(to_cents(price));
        int idx = cents - MIN_CENTS;

        price_qty[idx] += qty;
        order_map[id] = OrderData{side, cents, qty};

        if (side == Side::Buy) {
            if (best_bid_cents == -1 || cents > best_bid_cents) {
                best_bid_cents = cents;
            }
        } else {
            if (best_ask_cents == -1 || cents < best_ask_cents) {
                best_ask_cents = cents;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        auto map_it = order_map.find(id);
        if (map_it == order_map.end()) {
            return false; // Order not found or already cancelled
        }

        const auto& info = map_it->second;
        int cents = info.price_cents;
        int idx = cents - MIN_CENTS;

        price_qty[idx] -= info.qty;

        // If this price level is now depleted, we may need to advance the BBO pointer
        if (price_qty[idx] == 0) {
            if (info.side == Side::Buy) {
                if (cents == best_bid_cents) {
                    best_bid_cents = -1;
                    // Scan down for the next active bid level
                    for (int c = cents - 1; c >= Config::MIN_BID_CENTS; --c) {
                        if (price_qty[c - MIN_CENTS] > 0) {
                            best_bid_cents = c;
                            break;
                        }
                    }
                }
            } else {
                if (cents == best_ask_cents) {
                    best_ask_cents = -1;
                    // Scan up for the next active ask level
                    for (int c = cents + 1; c <= Config::MAX_ASK_CENTS; ++c) {
                        if (price_qty[c - MIN_CENTS] > 0) {
                            best_ask_cents = c;
                            break;
                        }
                    }
                }
            }
        }

        order_map.erase(map_it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};

        if (best_bid_cents != -1) {
            bbo.has_bid = true;
            bbo.bid_price = to_double(best_bid_cents);
            bbo.bid_qty = price_qty[best_bid_cents - MIN_CENTS];
        } else {
            bbo.has_bid = false;
            bbo.bid_price = 0.0;
            bbo.bid_qty = 0;
        }

        if (best_ask_cents != -1) {
            bbo.has_ask = true;
            bbo.ask_price = to_double(best_ask_cents);
            bbo.ask_qty = price_qty[best_ask_cents - MIN_CENTS];
        } else {
            bbo.has_ask = false;
            bbo.ask_price = 0.0;
            bbo.ask_qty = 0;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> levels;
        if (best_bid_cents == -1) return levels;

        levels.reserve(depth);
        int count = 0;
        for (int c = best_bid_cents; c >= Config::MIN_BID_CENTS && count < depth; --c) {
            long q = price_qty[c - MIN_CENTS];
            if (q > 0) {
                levels.push_back(Level{to_double(c), q});
                count++;
            }
        }
        return levels;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> levels;
        if (best_ask_cents == -1) return levels;

        levels.reserve(depth);
        int count = 0;
        for (int c = best_ask_cents; c <= Config::MAX_ASK_CENTS && count < depth; ++c) {
            long q = price_qty[c - MIN_CENTS];
            if (q > 0) {
                levels.push_back(Level{to_double(c), q});
                count++;
            }
        }
        return levels;
    }
};

using OrderBookC = OrderBookArray;
