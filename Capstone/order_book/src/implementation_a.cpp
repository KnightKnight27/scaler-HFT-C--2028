
#include "order_book.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <memory>

class MapOrderBook : public OrderBook {
private:
   
    using Price = int;

    struct Order {
        Side side;
        Price price;
        long qty;
    };

    
    std::map<Price, long, std::greater<Price>> bids;

   
    std::map<Price, long> asks;

   
    std::unordered_map<uint64_t, Order> orders;

    
    std::unordered_set<uint64_t> used_ids;

    static Price to_ticks(double price) {
        return static_cast<Price>(std::llround(price * 100.0));
    }

    static double to_price(Price ticks) {
        return ticks / 100.0;
    }

public:
    void add_order(
        uint64_t id,
        Side side,
        double price,
        long qty
    ) override {
        Price ticks = to_ticks(price);

        
        if (used_ids.count(id) != 0 || qty <= 0) {
            return;
        }

        if (side == Side::Buy) {
            if (ticks < 5000 || ticks > 9999) {
                return;
            }
        } else {
            if (ticks < 10001 || ticks > 15000) {
                return;
            }
        }

        used_ids.insert(id);
        orders.emplace(id, Order{side, ticks, qty});

        if (side == Side::Buy) {
            bids[ticks] += qty;
        } else {
            asks[ticks] += qty;
        }
    }

    bool cancel_order(uint64_t id) override {
        auto order_it = orders.find(id);

        
        if (order_it == orders.end()) {
            return false;
        }

        const Order order = order_it->second;

        if (order.side == Side::Buy) {
            auto level_it = bids.find(order.price);
            level_it->second -= order.qty;

            if (level_it->second == 0) {
                bids.erase(level_it);
            }
        } else {
            auto level_it = asks.find(order.price);
            level_it->second -= order.qty;

            if (level_it->second == 0) {
                asks.erase(level_it);
            }
        }

        orders.erase(order_it);
        return true;
    }

    BBO get_bbo() const override {
        BBO result;

        if (!bids.empty()) {
            auto best = bids.begin();
            result.has_bid = true;
            result.bid_price = to_price(best->first);
            result.bid_qty = best->second;
        }

        if (!asks.empty()) {
            auto best = asks.begin();
            result.has_ask = true;
            result.ask_price = to_price(best->first);
            result.ask_qty = best->second;
        }

        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        for (const auto& level : bids) {
            result.push_back(Level{to_price(level.first), level.second});

            if (static_cast<int>(result.size()) >= depth) {
                break;
            }
        }

        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        for (const auto& level : asks) {
            result.push_back(Level{to_price(level.first), level.second});

            if (static_cast<int>(result.size()) >= depth) {
                break;
            }
        }

        return result;
    }
};

std::unique_ptr<OrderBook> make_map_order_book() {
    return std::make_unique<MapOrderBook>();
}