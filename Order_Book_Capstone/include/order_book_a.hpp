#pragma once

#include "types.hpp"
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>
#include <stdexcept>

class OrderBookA {
private:
    struct Order {
        Side side;
        int32_t price;
        long qty;
    };

    std::unordered_map<uint64_t, Order> mOrders;

    std::map<int32_t, long, std::greater<int32_t>> mBids;
    std::map<int32_t, long, std::less<int32_t>> mAsks;

public:
    void add_order(uint64_t id, Side side, double price, long qty) {
        const int32_t intPrice = detail::price_to_int32(price);
        mOrders.emplace(id, Order{side, intPrice, qty});

        if (side == Side::Buy) {
            if (intPrice < detail::MIN_BID || intPrice > detail::MAX_BID) throw std::invalid_argument("Price out of bounds");
            mBids[intPrice] += qty;
        } else {
            if (intPrice < detail::MIN_ASK || intPrice > detail::MAX_ASK) throw std::invalid_argument("Price out of bounds");
            mAsks[intPrice] += qty;
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
            auto lvl_it = mBids.find(ord.price);
            if (lvl_it != mBids.end()) {
                lvl_it->second -= ord.qty;
                if (lvl_it->second <= 0) {
                    mBids.erase(lvl_it); 
                }
            }
        } else {
            auto lvl_it = mAsks.find(ord.price);
            if (lvl_it != mAsks.end()) {
                lvl_it->second -= ord.qty;
                if (lvl_it->second <= 0)
                    mAsks.erase(lvl_it);
            }
        }

        return true;
    }

    BBO get_bbo() const {
        BBO bbo{false, 0.0, 0, false, 0.0, 0};

        if (!mBids.empty()) {
            const auto& best_bid = *mBids.begin();
            bbo.has_bid   = true;
            bbo.bid_price = detail::int32_to_price(best_bid.first);
            bbo.bid_qty   = best_bid.second;
        }

        if (!mAsks.empty()) {
            const auto& best_ask = *mAsks.begin();
            bbo.has_ask   = true;
            bbo.ask_price = detail::int32_to_price(best_ask.first);
            bbo.ask_qty   = best_ask.second;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        int count = 0;
        for (const auto& [price, qty] : mBids) {
            if (count++ >= depth) break;
            result.push_back(Level{detail::int32_to_price(price), qty});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0) return result;
        result.reserve(depth);

        int count = 0;
        for (const auto& [price, qty] : mAsks) {
            if (count++ >= depth) break;
            result.push_back(Level{detail::int32_to_price(price), qty});
        }
        return result;
    }
};