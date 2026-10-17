#pragma once

#include "types.hpp"
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

class OrderBookB {
private:
    struct Order {
        Side side;
        int32_t price;
        long qty;
    };

    struct LevelState {
        long qty = 0;
        bool in_heap = false;
    };

    std::unordered_map<uint64_t, Order> mOrders;

    mutable std::unordered_map<int32_t, LevelState> mBidLevels;
    mutable std::unordered_map<int32_t, LevelState> mAskLevels;

    mutable std::priority_queue<int32_t, std::vector<int32_t>, std::less<int32_t>> mBidHeap;
    mutable std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>> mAskHeap;

    void purge_stale_bids() const {
        while (!mBidHeap.empty()) {
            const int32_t top_price = mBidHeap.top();
            auto it = mBidLevels.find(top_price);
            if (it == mBidLevels.end() || it->second.qty <= 0) {
                mBidHeap.pop();
                if (it != mBidLevels.end()) {
                    it->second.in_heap = false;
                }
            } else {
                break;
            }
        }
    }

    void purge_stale_asks() const {
        while (!mAskHeap.empty()) {
            const int32_t top_price = mAskHeap.top();
            auto it = mAskLevels.find(top_price);
            if (it == mAskLevels.end() || it->second.qty <= 0) {
                mAskHeap.pop();
                if (it != mAskLevels.end()) {
                    it->second.in_heap = false;
                }
            } else {
                break;
            }
        }
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        const int32_t intPrice = detail::price_to_int32(price);
        mOrders.emplace(id, Order{side, intPrice, qty});

        if (side == Side::Buy) {
            auto& state = mBidLevels[intPrice];
            state.qty += qty;
            if (!state.in_heap) {
                mBidHeap.push(intPrice);
                state.in_heap = true;
            }
        } else {
            auto& state = mAskLevels[intPrice];
            state.qty += qty;
            if (!state.in_heap) {
                mAskHeap.push(intPrice);
                state.in_heap = true;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = mOrders.find(id);
        if (it == mOrders.end()) {
            return false;
        }

        const Order ord = it->second;
        mOrders.erase(it);

        if (ord.side == Side::Buy) {
            auto lvl_it = mBidLevels.find(ord.price);
            if (lvl_it != mBidLevels.end()) {
                lvl_it->second.qty -= ord.qty;
            }
        } else {
            auto lvl_it = mAskLevels.find(ord.price);
            if (lvl_it != mAskLevels.end()) {
                lvl_it->second.qty -= ord.qty;
            }
        }

        return true;
    }

    BBO get_bbo() const {
        BBO bbo{false, 0.0, 0, false, 0.0, 0};

        purge_stale_bids();
        purge_stale_asks();
        
        if (!mBidHeap.empty()) {
            const int32_t best_bid = mBidHeap.top();
            bbo.has_bid   = true;
            bbo.bid_price = detail::int32_to_price(best_bid);
            bbo.bid_qty   = mBidLevels.at(best_bid).qty;
        }

        if (!mAskHeap.empty()) {
            const int32_t best_ask = mAskHeap.top();
            bbo.has_ask   = true;
            bbo.ask_price = detail::int32_to_price(best_ask);
            bbo.ask_qty   = mAskLevels.at(best_ask).qty;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        std::vector<int32_t> popped_prices;
        popped_prices.reserve(depth);

        while (static_cast<int>(result.size()) < depth) {
            purge_stale_bids();
            if (mBidHeap.empty()) break;

            const int32_t tick = mBidHeap.top();
            mBidHeap.pop();

            const long qty = mBidLevels.at(tick).qty;
            result.push_back(Level{detail::int32_to_price(tick), qty});
            popped_prices.push_back(tick);
        }

        for (const int32_t tick : popped_prices) {
            mBidHeap.push(tick);
        }

        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        std::vector<int32_t> popped_prices;
        popped_prices.reserve(depth);

        while (static_cast<int>(result.size()) < depth) {
            purge_stale_asks();
            if (mAskHeap.empty()) break;

            const int32_t tick = mAskHeap.top();
            mAskHeap.pop();

            const long qty = mAskLevels.at(tick).qty;
            result.push_back(Level{detail::int32_to_price(tick), qty});
            popped_prices.push_back(tick);
        }

        for (const int32_t tick : popped_prices) {
            mAskHeap.push(tick);
        }

        return result;
    }
};