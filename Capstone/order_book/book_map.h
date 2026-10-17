#pragma once
#include <functional>
#include <map>
#include <vector>
#include "common.h"

// Implementation A: everything is a tree
//
// std::map is a sorted container from the standard library
// inside it is a red black tree (a balanced binary search tree)
// every key is its own node on the heap and nodes point to each other
// so add, cancel and lookup are all log n
//
// orders: std::map from order id to Order
// levels: one std::map per side from tick to total qty
// best price: the first key of each map
class MapOrderBook {
public:
    void add_order(uint64_t id, Side side, int32_t tick, long qty) {
        if (!valid_order(side, tick, qty)) {
            return;
        }
        // emplace only inserts if the id is not already there
        // .second is false when the id already exists, then we ignore the order
        if (!orders_.emplace(id, Order{qty, tick, side}).second) {
            return;
        }
        // operator[] makes the level with qty 0 if it does not exist yet
        if (side == Side::Buy) {
            bids_[tick] += qty;
        } else {
            asks_[tick] += qty;
        }
    }

    bool cancel_order(uint64_t id) {
        std::map<uint64_t, Order>::iterator it = orders_.find(id);
        if (it == orders_.end()) {
            return false;  // unknown id or already cancelled
        }
        Order o = it->second;
        orders_.erase(it);

        if (o.side == Side::Buy) {
            BidMap::iterator lvl = bids_.find(o.tick);
            lvl->second -= o.qty;
            if (lvl->second == 0) {
                bids_.erase(lvl);  // no orders left at this price
            }
        } else {
            AskMap::iterator lvl = asks_.find(o.tick);
            lvl->second -= o.qty;
            if (lvl->second == 0) {
                asks_.erase(lvl);
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b = {false, 0, 0, false, 0, 0};
        // begin() of the bid map is the highest price because of std::greater
        // begin() is the leftmost node so the tree keeps it ready for us
        if (!bids_.empty()) {
            b.has_bid = true;
            b.bid_tick = bids_.begin()->first;
            b.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask = true;
            b.ask_tick = asks_.begin()->first;
            b.ask_qty = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (BidMap::const_iterator it = bids_.begin(); it != bids_.end(); ++it) {
            if ((int)out.size() >= depth) {
                break;
            }
            out.push_back(Level{it->first, it->second});
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (AskMap::const_iterator it = asks_.begin(); it != asks_.end(); ++it) {
            if ((int)out.size() >= depth) {
                break;
            }
            out.push_back(Level{it->first, it->second});
        }
        return out;
    }

private:
    // std::greater makes the map sort from high to low, so the best bid comes first
    typedef std::map<int32_t, long, std::greater<int32_t>> BidMap;
    typedef std::map<int32_t, long> AskMap;

    std::map<uint64_t, Order> orders_;
    BidMap bids_;
    AskMap asks_;
};
