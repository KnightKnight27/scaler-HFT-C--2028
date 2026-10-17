#pragma once

#include <cstdint>
#include <iterator>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "types.hpp"

// Implementation A: std::map of price levels, each level has a std::list of its orders
class MapOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = toTick(price);
        if (!isValid(side, tick, qty)) return;

        auto& levels = (side == Side::Buy) ? mBids : mAsks;
        PriceLevel& level = levels[tick];
        level.total += qty;
        level.orders.push_back({id, qty});
        mOrders[id] = {side, tick, std::prev(level.orders.end())};
    }

    bool cancel_order(uint64_t id) {
        auto it = mOrders.find(id);
        if (it == mOrders.end()) return false;

        OrderRef ref = it->second;
        auto& levels = (ref.side == Side::Buy) ? mBids : mAsks;
        auto lvl = levels.find(ref.tick);
        lvl->second.total -= ref.pos->qty;
        lvl->second.orders.erase(ref.pos);
        if (lvl->second.orders.empty()) levels.erase(lvl);
        mOrders.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (!mBids.empty()) {
            auto best = mBids.rbegin();
            bbo.has_bid = true;
            bbo.bid_price = toPrice(best->first);
            bbo.bid_qty = best->second.total;
        }
        if (!mAsks.empty()) {
            auto best = mAsks.begin();
            bbo.has_ask = true;
            bbo.ask_price = toPrice(best->first);
            bbo.ask_qty = best->second.total;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = mBids.rbegin(); it != mBids.rend() && (int)out.size() < depth; ++it)
            out.push_back({toPrice(it->first), it->second.total});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = mAsks.begin(); it != mAsks.end() && (int)out.size() < depth; ++it)
            out.push_back({toPrice(it->first), it->second.total});
        return out;
    }

private:
    struct Order {
        uint64_t id;
        long qty;
    };

    struct PriceLevel {
        long total = 0;
        std::list<Order> orders;
    };

    struct OrderRef {
        Side side;
        int tick;
        std::list<Order>::iterator pos;
    };

    std::map<int, PriceLevel> mBids;
    std::map<int, PriceLevel> mAsks;
    std::unordered_map<uint64_t, OrderRef> mOrders;
};
