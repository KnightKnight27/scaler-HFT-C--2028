#pragma once

#include "types.hpp"
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>

class OrderBookC {
private:
    struct Order {
        Side side;
        int32_t tick;
        long qty;
    };

    std::unordered_map<uint64_t, Order> mOrders;

    std::array<long, 5000> mBidQuantities{};
    std::array<long, 5000> mAskQuantities{};

    mutable int mBestBidOffset = -1;
    mutable int mBestAskOffset = -1;

    void update_best_bid_on_add(int offset) {
        if (mBestBidOffset == -1 || offset > mBestBidOffset) {
            mBestBidOffset = offset;
        }
    }

    void update_best_ask_on_add(int offset) {
        if (mBestAskOffset == -1 || offset < mBestAskOffset) {
            mBestAskOffset = offset;
        }
    }

    void refresh_best_bid() const {
        while (mBestBidOffset >= 0 && mBidQuantities[mBestBidOffset] <= 0) {
            mBestBidOffset--;
        }
    }

    void refresh_best_ask() const {
        while (mBestAskOffset < 5000 && mBestAskOffset >= 0 && mAskQuantities[mBestAskOffset] <= 0) {
            mBestAskOffset++;
            if (mBestAskOffset >= 5000 || mAskQuantities[mBestAskOffset] > 0) break;
        }
        if (mBestAskOffset >= 5000 || (mBestAskOffset >= 0 && mAskQuantities[mBestAskOffset] <= 0)) {
            mBestAskOffset = -1;
            for (int i = 0; i < 5000; ++i) {
                if (mAskQuantities[i] > 0) {
                    mBestAskOffset = i;
                    break;
                }
            }
        }
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        const int32_t intPrice = detail::price_to_int32(price);
        mOrders.emplace(id, Order{side, intPrice, qty});

        if (side == Side::Buy) {
            if (intPrice < detail::MIN_BID || intPrice > detail::MAX_BID) {
                throw std::invalid_argument("Bid price out of range");
            }
            int offset = intPrice - 5000;
            if (offset >= 0 && offset < 5000) {
                mBidQuantities[offset] += qty;
                update_best_bid_on_add(offset);
            }   
        } else {
            if (intPrice < detail::MIN_ASK || intPrice > detail::MAX_ASK) {
                throw std::invalid_argument("Ask price out of range");
            }
            int offset = intPrice - 10001;
            if (offset >= 0 && offset < 5000) {
                mAskQuantities[offset] += qty;
                update_best_ask_on_add(offset);
            }
        }
    }

    bool cancel_order(uint64_t id) {
        auto it = mOrders.find(id);
        if (it == mOrders.end()) return false;

        const Order ord = it->second;
        mOrders.erase(it);

        if (ord.side == Side::Buy) {
            int offset = ord.tick - 5000;
            if (offset >= 0 && offset < 5000) {
                mBidQuantities[offset] -= ord.qty;
            }
        } else {
            int offset = ord.tick - 10001;
            if (offset >= 0 && offset < 5000) {
                mAskQuantities[offset] -= ord.qty;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO bbo{false, 0.0, 0, false, 0.0, 0};

        refresh_best_bid();
        if (mBestBidOffset >= 0 && mBidQuantities[mBestBidOffset] > 0) {
            bbo.has_bid = true;
            bbo.bid_price = detail::int32_to_price(5000 + mBestBidOffset);
            bbo.bid_qty = mBidQuantities[mBestBidOffset];
        }

        refresh_best_ask();
        if (mBestAskOffset >= 0 && mBestAskOffset < 5000 && mAskQuantities[mBestAskOffset] > 0) {
            bbo.has_ask = true;
            bbo.ask_price = detail::int32_to_price(10001 + mBestAskOffset);
            bbo.ask_qty = mAskQuantities[mBestAskOffset];
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        int count = 0;
        for (int i = 4999; i >= 0 && count < depth; --i) {
            if (mBidQuantities[i] > 0) {
                result.push_back(Level{detail::int32_to_price(5000 + i), mBidQuantities[i]});
                count++;
            }
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        int count = 0;
        for (int i = 0; i < 5000 && count < depth; ++i) {
            if (mAskQuantities[i] > 0) {
                result.push_back(Level{detail::int32_to_price(10001 + i), mAskQuantities[i]});
                count++;
            }
        }
        return result;
    }
};