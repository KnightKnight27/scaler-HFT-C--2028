#ifndef ORDERBOOK_BOOK_A_HPP
#define ORDERBOOK_BOOK_A_HPP

#include <map>
#include <unordered_map>

#include "common.hpp"

//book a: price levels in a std::map, orders in an unordered_map by id
class MapOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        orders_[id] = Order{side, tick, qty};

        if (side == Side::Buy) {
            bids_[tick] += qty;
        } else {
            asks_[tick] += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        Order order = it->second;
        orders_.erase(it);

        std::map<int, long>& levels = (order.side == Side::Buy) ? bids_ : asks_;
        auto level = levels.find(order.tick);
        level->second -= order.qty;
        if (level->second == 0) {
            levels.erase(level);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            auto best = bids_.rbegin();  //map is sorted low to high, so the best bid is the last one
            bbo.has_bid = true;
            bbo.bid_price = to_price(best->first);
            bbo.bid_qty = best->second;
        }
        if (!asks_.empty()) {
            auto best = asks_.begin();
            bbo.has_ask = true;
            bbo.ask_price = to_price(best->first);
            bbo.ask_qty = best->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = bids_.rbegin(); it != bids_.rend(); ++it) {
            if ((int)out.size() >= depth) break;
            out.push_back({to_price(it->first), it->second});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = asks_.begin(); it != asks_.end(); ++it) {
            if ((int)out.size() >= depth) break;
            out.push_back({to_price(it->first), it->second});
        }
        return out;
    }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    std::unordered_map<uint64_t, Order> orders_;
    std::map<int, long> bids_;  //tick -> total qty
    std::map<int, long> asks_;
};

#endif
