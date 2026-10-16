
#include "order_book.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class HeapOrderBook : public OrderBook {
private:
    using Price = int;

    struct Order {
        Side side;
        Price price;
        long qty;
    };

    
    std::priority_queue<Price> bid_heap;

    
    std::priority_queue<
        Price,
        std::vector<Price>,
        std::greater<Price>
    > ask_heap;

    
    std::unordered_map<Price, long> bids;
    std::unordered_map<Price, long> asks;

    
    std::unordered_map<uint64_t, Order> orders;

    
    std::unordered_set<uint64_t> used_ids;

    static Price to_ticks(double price) {
        return static_cast<Price>(std::llround(price * 100.0));
    }

    static double to_price(Price ticks) {
        return ticks / 100.0;
    }

    void clean_bid_heap() {
        while (!bid_heap.empty()) {
            Price price = bid_heap.top();
            auto it = bids.find(price);

            if (it != bids.end() && it->second > 0) {
                break;
            }

            bid_heap.pop();
        }
    }

    void clean_ask_heap() {
        while (!ask_heap.empty()) {
            Price price = ask_heap.top();
            auto it = asks.find(price);

            if (it != asks.end() && it->second > 0) {
                break;
            }

            ask_heap.pop();
        }
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
            auto result = bids.emplace(ticks, qty);

            if (result.second) {
                bid_heap.push(ticks);
            } else {
                result.first->second += qty;
            }
        } else {
            auto result = asks.emplace(ticks, qty);

            if (result.second) {
                ask_heap.push(ticks);
            } else {
                result.first->second += qty;
            }
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
        
        auto* self = const_cast<HeapOrderBook*>(this);
        self->clean_bid_heap();
        self->clean_ask_heap();

        BBO result;

        if (!bid_heap.empty()) {
            Price price = bid_heap.top();
            result.has_bid = true;
            result.bid_price = to_price(price);
            result.bid_qty = bids.at(price);
        }

        if (!ask_heap.empty()) {
            Price price = ask_heap.top();
            result.has_ask = true;
            result.ask_price = to_price(price);
            result.ask_qty = asks.at(price);
        }

        return result;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
    
        if (depth <= 0) {
            return result;
        }
    
        auto heap = bid_heap;
        std::unordered_set<Price> seen;
    
        while (!heap.empty() &&
               static_cast<int>(result.size()) < depth) {
            Price price = heap.top();
            heap.pop();
    
            
            auto it = bids.find(price);
            if (it == bids.end()) {
                continue;
            }
    
            
            if (!seen.insert(price).second) {
                continue;
            }
    
            result.push_back(Level{to_price(price), it->second});
        }
    
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
    
        if (depth <= 0) {
            return result;
        }
    
        auto heap = ask_heap;
        std::unordered_set<Price> seen;
    
        while (!heap.empty() &&
               static_cast<int>(result.size()) < depth) {
            Price price = heap.top();
            heap.pop();
    
            
            auto it = asks.find(price);
            if (it == asks.end()) {
                continue;
            }
    
            
            if (!seen.insert(price).second) {
                continue;
            }
    
            result.push_back(Level{to_price(price), it->second});
        }
    
        return result;
    }
};

std::unique_ptr<OrderBook> make_heap_order_book() {
    return std::make_unique<HeapOrderBook>();
}
