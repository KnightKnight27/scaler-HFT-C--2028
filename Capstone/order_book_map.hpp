#pragma once

#include "order_book.hpp"
#include <map>
#include <unordered_map>
#include <cmath>

// Implementation A: Standard Red-Black Tree (std::map)
class OrderBookMap : public IOrderBook {
private:
    struct OrderRecord {
        Side side;
        int32_t tick;
        long qty;
    };

    // Bids sorted descending (highest buy price first)
    std::map<int32_t, long, std::greater<int32_t>> bids_;
    // Asks sorted ascending (lowest sell price first)
    std::map<int32_t, long, std::less<int32_t>> asks_;

    // Fast order ID lookup for O(1) order location before tree removal
    std::unordered_map<uint64_t, OrderRecord> orders_;

    static int32_t price_to_tick(double price) {
        return static_cast<int32_t>(std::lround(price * 100.0));
    }

    static double tick_to_price(int32_t tick) {
        return tick / 100.0;
    }

public:
    OrderBookMap() = default;

    void add_order(uint64_t id, Side side, double price, long qty) override {
        int32_t tick = price_to_tick(price);
        if (side == Side::Buy) {
            bids_[tick] += qty;
        } else {
            asks_[tick] += qty;
        }
        orders_[id] = {side, tick, qty};
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }

        const OrderRecord& rec = it->second;
        if (rec.side == Side::Buy) {
            auto bit = bids_.find(rec.tick);
            if (bit != bids_.end()) {
                bit->second -= rec.qty;
                if (bit->second <= 0) {
                    bids_.erase(bit);
                }
            }
        } else {
            auto ait = asks_.find(rec.tick);
            if (ait != asks_.end()) {
                ait->second -= rec.qty;
                if (ait->second <= 0) {
                    asks_.erase(ait);
                }
            }
        }

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo;
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(bids_.begin()->first);
            bbo.bid_qty = bids_.begin()->second;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(asks_.begin()->first);
            bbo.ask_qty = asks_.begin()->second;
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        int count = 0;
        for (const auto& [tick, qty] : bids_) {
            if (count++ >= depth) break;
            result.push_back({tick_to_price(tick), qty});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        result.reserve(depth);
        int count = 0;
        for (const auto& [tick, qty] : asks_) {
            if (count++ >= depth) break;
            result.push_back({tick_to_price(tick), qty});
        }
        return result;
    }
};
