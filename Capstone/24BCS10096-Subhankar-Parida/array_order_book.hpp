#pragma once

#include <cstdint>
#include <vector>

#include "types.hpp"

// Implementation C (Our Own Design): Fixed-size array indexed by price tick.
// There are only 10,001 possible price ticks (5000..15000), so we allocate
// one slot per tick and store the aggregate quantity directly. The best bid
// and best ask are tracked as integers. Orders are stored in a flat vector
// indexed by order ID (IDs must be < MAX_ORDERS).
//
// This gives O(1) add, O(1) cancel (amortized), and O(1) BBO.
class ArrayOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = toTick(price);
        if (!isValid(side, tick, qty) || id >= MAX_ORDERS) return;

        if (id >= mOrders.size()) mOrders.resize(id + 1);
        mOrders[id] = {qty, tick};
        mQty[tick - MIN_BID] += qty;

        if (side == Side::Buy  && tick > mBestBid) mBestBid = tick;
        if (side == Side::Sell && tick < mBestAsk) mBestAsk = tick;
    }

    bool cancel_order(uint64_t id) {
        if (id >= mOrders.size() || mOrders[id].qty == 0) return false;

        Order& order = mOrders[id];
        mQty[order.tick - MIN_BID] -= order.qty;
        order.qty = 0;

        // If the best level became empty, walk to the next non-empty price
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
        long qty;   // 0 = cancelled / unused
        int tick;
    };

    static constexpr uint64_t MAX_ORDERS = 1 << 24;  // ~16M orders max

    long mQty[MAX_ASK - MIN_BID + 1] = {};    // qty per tick, zero-initialized
    int mBestBid = MIN_BID - 1;               // no bids sentinel
    int mBestAsk = MAX_ASK + 1;               // no asks sentinel
    std::vector<Order> mOrders;
};
