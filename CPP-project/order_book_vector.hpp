#pragma once

#include "order_book.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace std;

class OrderBookVector : public OrderBook {
private:
    struct LevelTick {
        uint32_t tick;
        long qty;
    };

    struct OrderRecord {
        Side side;
        uint32_t tick;
        long qty;
    };

    vector<LevelTick> bids_;
    vector<LevelTick> asks_;
    unordered_map<uint64_t, OrderRecord> orders_;

    template <typename Comp>
    static void update_or_insert(vector<LevelTick>& levels, uint32_t tick, long qty, Comp comp) {
        auto it = lower_bound(levels.begin(), levels.end(), tick, comp);
        if (it != levels.end() && it->tick == tick) {
            it->qty += qty;
        } else {
            levels.insert(it, LevelTick{tick, qty});
        }
    }

    template <typename Comp>
    static void remove_qty(vector<LevelTick>& levels, uint32_t tick, long qty, Comp comp) {
        auto it = lower_bound(levels.begin(), levels.end(), tick, comp);
        if (it != levels.end() && it->tick == tick && (it->qty -= qty) <= 0) {
            levels.erase(it);
        }
    }

    static vector<Level> extract_depth(const vector<LevelTick>& levels, int depth) {
        int count = min(depth, static_cast<int>(levels.size()));
        vector<Level> result;
        result.reserve(count);
        for (int i = 0; i < count; ++i) {
            result.push_back({tick_to_price(levels[i].tick), levels[i].qty});
        }
        return result;
    }

public:
    void add_order(uint64_t id, Side side, double price, long qty) override {
        uint32_t tick = price_to_tick(price);
        orders_[id] = OrderRecord{side, tick, qty};

        if (side == Side::Buy) {
            update_or_insert(bids_, tick, qty, [](const LevelTick& a, uint32_t t) { return a.tick > t; });
        } else {
            update_or_insert(asks_, tick, qty, [](const LevelTick& a, uint32_t t) { return a.tick < t; });
        }
    }

    bool cancel_order(uint64_t id) override {
        auto oit = orders_.find(id);
        if (oit == orders_.end()) {
            return false;
        }

        const auto& ord = oit->second;
        if (ord.side == Side::Buy) {
            remove_qty(bids_, ord.tick, ord.qty, [](const LevelTick& a, uint32_t t) { return a.tick > t; });
        } else {
            remove_qty(asks_, ord.tick, ord.qty, [](const LevelTick& a, uint32_t t) { return a.tick < t; });
        }

        orders_.erase(oit);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo{};
        if (!bids_.empty()) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(bids_.front().tick);
            bbo.bid_qty = bids_.front().qty;
        }
        if (!asks_.empty()) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(asks_.front().tick);
            bbo.ask_qty = asks_.front().qty;
        }
        return bbo;
    }

    vector<Level> get_bids(int depth = 10) const override {
        return extract_depth(bids_, depth);
    }

    vector<Level> get_asks(int depth = 10) const override {
        return extract_depth(asks_, depth);
    }
};
