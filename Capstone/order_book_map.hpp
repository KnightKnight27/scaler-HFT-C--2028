#pragma once

// Implementation A: std::map (Red-Black Tree) + std::unordered_map
// Uses std::map for ordered price levels and std::unordered_map for O(1) order cancellation lookup.
// Inside each price level, orders are stored in a std::list.

#include "common.hpp"
#include <vector>
#include <unordered_map>
#include <map>
#include <list>
#include <cstdint>

class OrderBookMap {
private:
    struct Order {
        uint64_t id;
        Side side;
        int64_t price_cents;
        long qty;
    };

    using ListIterator = std::list<Order>::iterator;

    struct MapValue {
        int64_t price_cents;
        Side side;
        ListIterator it;
    };

    // Fast order lookup by ID for cancellation
    std::unordered_map<uint64_t, MapValue> order_map;

    // Price trees:
    // Bids ordered descending (highest buy price first)
    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bid_tree;
    // Asks ordered ascending (lowest sell price first)
    std::map<int64_t, std::list<Order>, std::less<int64_t>> ask_tree;

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int64_t price_cents = to_cents(price);
        Order new_order{id, side, price_cents, qty};

        if (side == Side::Buy) {
            auto& order_list = bid_tree[price_cents];
            order_list.push_back(new_order);
            order_map[id] = MapValue{price_cents, side, std::prev(order_list.end())};
        } else {
            auto& order_list = ask_tree[price_cents];
            order_list.push_back(new_order);
            order_map[id] = MapValue{price_cents, side, std::prev(order_list.end())};
        }
    }

    bool cancel_order(uint64_t id) {
        auto map_it = order_map.find(id);
        if (map_it == order_map.end()) {
            return false; // Order not found or already cancelled
        }

        const auto& metadata = map_it->second;
        if (metadata.side == Side::Buy) {
            auto tree_it = bid_tree.find(metadata.price_cents);
            if (tree_it != bid_tree.end()) {
                tree_it->second.erase(metadata.it);
                if (tree_it->second.empty()) {
                    bid_tree.erase(tree_it);
                }
            }
        } else {
            auto tree_it = ask_tree.find(metadata.price_cents);
            if (tree_it != ask_tree.end()) {
                tree_it->second.erase(metadata.it);
                if (tree_it->second.empty()) {
                    ask_tree.erase(tree_it);
                }
            }
        }

        order_map.erase(map_it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};

        if (!bid_tree.empty()) {
            auto it = bid_tree.begin();
            bbo.has_bid = true;
            bbo.bid_price = to_double(it->first);

            long total_qty = 0;
            for (const auto& order : it->second) {
                total_qty += order.qty;
            }
            bbo.bid_qty = total_qty;
        } else {
            bbo.has_bid = false;
            bbo.bid_price = 0.0;
            bbo.bid_qty = 0;
        }

        if (!ask_tree.empty()) {
            auto it = ask_tree.begin();
            bbo.has_ask = true;
            bbo.ask_price = to_double(it->first);

            long total_qty = 0;
            for (const auto& order : it->second) {
                total_qty += order.qty;
            }
            bbo.ask_qty = total_qty;
        } else {
            bbo.has_ask = false;
            bbo.ask_price = 0.0;
            bbo.ask_qty = 0;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> levels;
        levels.reserve(depth);

        int count = 0;
        for (auto it = bid_tree.begin(); it != bid_tree.end() && count < depth; ++it) {
            long total_qty = 0;
            for (const auto& order : it->second) {
                total_qty += order.qty;
            }
            levels.push_back(Level{to_double(it->first), total_qty});
            count++;
        }
        return levels;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> levels;
        levels.reserve(depth);

        int count = 0;
        for (auto it = ask_tree.begin(); it != ask_tree.end() && count < depth; ++it) {
            long total_qty = 0;
            for (const auto& order : it->second) {
                total_qty += order.qty;
            }
            levels.push_back(Level{to_double(it->first), total_qty});
            count++;
        }
        return levels;
    }
};

using OrderBookA = OrderBookMap;
