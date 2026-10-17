#ifndef ORDERBOOK_BOOK_B_HPP
#define ORDERBOOK_BOOK_B_HPP

#include <map>

#include "common.hpp"

//book b: each side is a sorted std::vector of levels, orders in a std::map by id
class VectorOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = to_tick(price);
        orders_[id] = Order{side, tick, qty};

        std::vector<TickLevel>& levels = (side == Side::Buy) ? bids_ : asks_;
        int i = find_level(levels, tick);
        if (i < (int)levels.size() && levels[i].tick == tick) {
            levels[i].qty += qty;
        } else {
            levels.insert(levels.begin() + i, TickLevel{tick, qty});  //everything after i shifts right
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        Order order = it->second;
        orders_.erase(it);

        std::vector<TickLevel>& levels = (order.side == Side::Buy) ? bids_ : asks_;
        int i = find_level(levels, order.tick);
        levels[i].qty -= order.qty;
        if (levels[i].qty == 0) {
            levels.erase(levels.begin() + i);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo;
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = to_price(bids_.back().tick);
            bbo.bid_qty = bids_.back().qty;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = to_price(asks_.front().tick);
            bbo.ask_qty = asks_.front().qty;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int i = (int)bids_.size() - 1; i >= 0; i--) {
            if ((int)out.size() >= depth) break;
            out.push_back({to_price(bids_[i].tick), bids_[i].qty});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int i = 0; i < (int)asks_.size(); i++) {
            if ((int)out.size() >= depth) break;
            out.push_back({to_price(asks_[i].tick), asks_[i].qty});
        }
        return out;
    }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    struct TickLevel {
        int tick;
        long qty;
    };

    std::map<uint64_t, Order> orders_;

    //both sides sorted low to high, so best bid is back() and best ask is front()
    std::vector<TickLevel> bids_;
    std::vector<TickLevel> asks_;

    //binary search, returns where the tick is or where it should be inserted
    static int find_level(const std::vector<TickLevel>& levels, int tick) {
        int lo = 0;
        int hi = (int)levels.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (levels[mid].tick < tick) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }
};

#endif
