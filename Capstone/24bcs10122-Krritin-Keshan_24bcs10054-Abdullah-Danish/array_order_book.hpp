#pragma once

#include <cstdint>
#include <vector>

#include "types.hpp"

// Implementation C: one array slot per price tick, orders kept in a vector by id
class ArrayOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = toTick(price);
        if (!isValid(side, tick, qty) || id >= MAX_ORDERS) return;

        if (id >= mOrders.size()) mOrders.resize(id + 1);
        mOrders[id] = {qty, tick};
        mQty[tick - MIN_BID] += qty;

        if (side == Side::Buy && tick > mBestBid) mBestBid = tick;
        if (side == Side::Sell && tick < mBestAsk) mBestAsk = tick;
    }

    bool cancel_order(uint64_t id) {
        if (id >= mOrders.size() || mOrders[id].qty == 0) return false;

        Order& order = mOrders[id];
        mQty[order.tick - MIN_BID] -= order.qty;
        order.qty = 0;

        // if the best level became empty -> walk to the next price that has orders
        while (mBestBid >= MIN_BID && mQty[mBestBid - MIN_BID] == 0) mBestBid--;
        while (mBestAsk <= MAX_ASK && mQty[mBestAsk - MIN_BID] == 0) mBestAsk++;
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (mBestBid >= MIN_BID) {
            bbo.has_bid = true;
            bbo.bid_price = toPrice(mBestBid);
            bbo.bid_qty = mQty[mBestBid - MIN_BID];
        }
        if (mBestAsk <= MAX_ASK) {
            bbo.has_ask = true;
            bbo.ask_price = toPrice(mBestAsk);
            bbo.ask_qty = mQty[mBestAsk - MIN_BID];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int t = mBestBid; t >= MIN_BID && (int)out.size() < depth; t--)
            if (mQty[t - MIN_BID] > 0) out.push_back({toPrice(t), mQty[t - MIN_BID]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int t = mBestAsk; t <= MAX_ASK && (int)out.size() < depth; t++)
            if (mQty[t - MIN_BID] > 0) out.push_back({toPrice(t), mQty[t - MIN_BID]});
        return out;
    }

private:
    struct Order {
        long qty;   // 0 = cancelled
        int tick;
    };

    static constexpr uint64_t MAX_ORDERS = 1 << 24;

    long mQty[MAX_ASK - MIN_BID + 1] = {};
    int mBestBid = MIN_BID - 1;   // MIN_BID - 1 = no bids
    int mBestAsk = MAX_ASK + 1;   // MAX_ASK + 1 = no asks
    std::vector<Order> mOrders;
};
