#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <unordered_map>
#include <vector>

#include "types.hpp"

// Implementation B: priority_queue of prices with lazy deletion.
// an empty level is not removed from the heap right away, it is popped when it reaches the top
class HeapOrderBook {
public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick = toTick(price);
        if (!isValid(side, tick, qty)) return;

        mOrders[id] = {side, tick, qty};
        LevelInfo& level = mLevels[tick];
        level.qty += qty;
        if (!level.inHeap) {
            level.inHeap = true;
            if (side == Side::Buy) mBidHeap.push(tick);
            else mAskHeap.push(tick);
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = mOrders.find(id);
        if (it == mOrders.end()) return false;

        Order order = it->second;
        mOrders.erase(it);
        mLevels[order.tick].qty -= order.qty;

        if (order.side == Side::Buy) {
            while (!mBidHeap.empty() && mLevels[mBidHeap.top()].qty == 0) {
                mLevels[mBidHeap.top()].inHeap = false;
                mBidHeap.pop();
            }
        } else {
            while (!mAskHeap.empty() && mLevels[mAskHeap.top()].qty == 0) {
                mLevels[mAskHeap.top()].inHeap = false;
                mAskHeap.pop();
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (!mBidHeap.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = toPrice(mBidHeap.top());
            bbo.bid_qty = mLevels.at(mBidHeap.top()).qty;
        }
        if (!mAskHeap.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = toPrice(mAskHeap.top());
            bbo.ask_qty = mLevels.at(mAskHeap.top()).qty;
        }
        return bbo;
    }

    // heap is not sorted, so we pop from a copy of it
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        auto heap = mBidHeap;
        while (!heap.empty() && (int)out.size() < depth) {
            long qty = mLevels.at(heap.top()).qty;
            if (qty > 0) out.push_back({toPrice(heap.top()), qty});
            heap.pop();
        }
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        auto heap = mAskHeap;
        while (!heap.empty() && (int)out.size() < depth) {
            long qty = mLevels.at(heap.top()).qty;
            if (qty > 0) out.push_back({toPrice(heap.top()), qty});
            heap.pop();
        }
        return out;
    }

private:
    struct Order {
        Side side;
        int tick;
        long qty;
    };

    struct LevelInfo {
        long qty = 0;
        bool inHeap = false;
    };

    std::priority_queue<int> mBidHeap;
    std::priority_queue<int, std::vector<int>, std::greater<int>> mAskHeap;
    std::unordered_map<int, LevelInfo> mLevels;
    std::map<uint64_t, Order> mOrders;
};
