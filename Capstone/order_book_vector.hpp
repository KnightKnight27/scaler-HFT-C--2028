#pragma once

#include "order_book.hpp"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>

// Implementation B: Contiguous Flat Sorted Vector (Sorted Array of Levels)
class OrderBookVector : public IOrderBook {
private:
    struct LevelInternal {
        int32_t tick;
        long qty;
    };

    struct OrderRecord {
        Side side;
        int32_t tick;
        long qty;
    };

    // Bids stored contiguous, sorted descending by tick
    std::vector<LevelInternal> bids_;
    // Asks stored contiguous, sorted ascending by tick
    std::vector<LevelInternal> asks_;

    // Fast order ID lookup
    std::unordered_map<uint64_t, OrderRecord> orders_;

    static int32_t price_to_tick(double price) {
        return static_cast<int32_t>(std::lround(price * 100.0));
    }

    static double tick_to_price(int32_t tick) {
        return tick / 100.0;
    }

public:
    OrderBookVector() {
        bids_.reserve(256);
        asks_.reserve(256);
    }

    void add_order(uint64_t id, Side side, double price, long qty) override {
        int32_t tick = price_to_tick(price);

        if (side == Side::Buy) {
            // Descending order for bids: greater tick comes first
            auto it = std::lower_bound(bids_.begin(), bids_.end(), tick, 
                [](const LevelInternal& lvl, int32_t val) {
                    return lvl.tick > val;
                });
            if (it != bids_.end() && it->tick == tick) {
                it->qty += qty;
            } else {
                bids_.insert(it, {tick, qty});
            }
        } else {
            // Ascending order for asks: smaller tick comes first
            auto it = std::lower_bound(asks_.begin(), asks_.end(), tick, 
                [](const LevelInternal& lvl, int32_t val) {
                    return lvl.tick < val;
                });
            if (it != asks_.end() && it->tick == tick) {
                it->qty += qty;
            } else {
                asks_.insert(it, {tick, qty});
            }
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
            auto bit = std::lower_bound(bids_.begin(), bids_.end(), rec.tick, 
                [](const LevelInternal& lvl, int32_t val) {
                    return lvl.tick > val;
                });
            if (bit != bids_.end() && bit->tick == rec.tick) {
                bit->qty -= rec.qty;
                if (bit->qty <= 0) {
                    bids_.erase(bit);
                }
            }
        } else {
            auto ait = std::lower_bound(asks_.begin(), asks_.end(), rec.tick, 
                [](const LevelInternal& lvl, int32_t val) {
                    return lvl.tick < val;
                });
            if (ait != asks_.end() && ait->tick == rec.tick) {
                ait->qty -= rec.qty;
                if (ait->qty <= 0) {
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

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        int n = std::min<int>(depth, static_cast<int>(bids_.size()));
        result.reserve(n);
        for (int i = 0; i < n; ++i) {
            result.push_back({tick_to_price(bids_[i].tick), bids_[i].qty});
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        int n = std::min<int>(depth, static_cast<int>(asks_.size()));
        result.reserve(n);
        for (int i = 0; i < n; ++i) {
            result.push_back({tick_to_price(asks_[i].tick), asks_[i].qty});
        }
        return result;
    }
};
