#pragma once
// in this is hvae used map and redblack tree (it increase bbo but sloww for add and clancle relaize after implemating)
#include <vector>
#include <unordered_map>
#include <map>
#include <list>
#include <cstdint>

enum class Side { Buy, Sell };

struct Level {
    double price;
    long qty;
};

struct BBO {
    bool has_bid;
    double bid_price;
    long bid_qty;

    bool has_ask;
    double ask_price;
    long ask_qty;
};

class OrderBook {
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

    
    std::unordered_map<uint64_t, MapValue> order_map;

     
    std::map<int64_t, std::list<Order>, std::greater<int64_t>> bid_tree;
    
    std::map<int64_t, std::list<Order>, std::less<int64_t>> ask_tree;

    
    inline int64_t to_cents(double price) const {
        return static_cast<int64_t>(price * 100.0 + 0.5);
    }

    inline double to_double(int64_t cents) const {
        return static_cast<double>(cents) / 100.0;
    }

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
            return false; 
        }

        const auto& metadata = map_it->second;
        if (metadata.side == Side::Buy) {
            auto tree_it = bid_tree.find(metadata.price_cents);
            tree_it->second.erase(metadata.it); // O(1) removal from list
            
            if (tree_it->second.empty()) {
                bid_tree.erase(tree_it); // O(log N) tree node removal if empty
            }
        } else {
            auto tree_it = ask_tree.find(metadata.price_cents);
            tree_it->second.erase(metadata.it); // O(1) removal from list
            
            if (tree_it->second.empty()) {
                ask_tree.erase(tree_it); // O(log N) tree node removal if empty
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
