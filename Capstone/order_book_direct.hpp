#pragma once

#include "order_book.hpp"
#include <array>
#include <unordered_map>
#include <cmath>

// Implementation C: Custom Design - Direct Price-Indexed Flat Array with Cached BBO Tracking
class OrderBookDirect : public IOrderBook {
private:
    static constexpr int32_t MIN_TICK = 5000;   // 50.00
    static constexpr int32_t MAX_TICK = 15000;  // 150.00
    static constexpr int32_t NUM_LEVELS = MAX_TICK - MIN_TICK + 1; // 10001 levels

    struct OrderRecord {
        Side side;
        int32_t idx;
        long qty;
    };

    // Dense flat array: 10,001 slots (~80 KB), fits cleanly in L2 cache
    std::array<long, NUM_LEVELS> level_qty_{};

    // Fast order ID lookup
    std::unordered_map<uint64_t, OrderRecord> orders_;

    // Cached BBO level indices for instantaneous O(1) BBO queries
    int32_t best_bid_idx_{-1};
    int32_t best_ask_idx_{NUM_LEVELS};

    static int32_t price_to_idx(double price) {
        int32_t tick = static_cast<int32_t>(std::lround(price * 100.0));
        return tick - MIN_TICK;
    }

    static double idx_to_price(int32_t idx) {
        return (idx + MIN_TICK) / 100.0;
    }

public:
    OrderBookDirect() = default;

    void add_order(uint64_t id, Side side, double price, long qty) override {
        int32_t idx = price_to_idx(price);
        if (idx < 0 || idx >= NUM_LEVELS) return;

        level_qty_[idx] += qty;

        if (side == Side::Buy) {
            if (idx > best_bid_idx_) {
                best_bid_idx_ = idx;
            }
        } else {
            if (idx < best_ask_idx_) {
                best_ask_idx_ = idx;
            }
        }

        orders_[id] = {side, idx, qty};
    }

    bool cancel_order(uint64_t id) override {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }

        const OrderRecord& rec = it->second;
        int32_t idx = rec.idx;
        level_qty_[idx] -= rec.qty;

        if (level_qty_[idx] <= 0) {
            level_qty_[idx] = 0;
            // If the cleared level was the best bid or ask, scan for next active level
            if (idx == best_bid_idx_) {
                int32_t next_idx = best_bid_idx_ - 1;
                while (next_idx >= 0 && level_qty_[next_idx] == 0) {
                    --next_idx;
                }
                best_bid_idx_ = next_idx;
            } else if (idx == best_ask_idx_) {
                int32_t next_idx = best_ask_idx_ + 1;
                while (next_idx < NUM_LEVELS && level_qty_[next_idx] == 0) {
                    ++next_idx;
                }
                best_ask_idx_ = next_idx;
            }
        }

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const override {
        BBO bbo;
        if (best_bid_idx_ >= 0) {
            bbo.has_bid = true;
            bbo.bid_price = idx_to_price(best_bid_idx_);
            bbo.bid_qty = level_qty_[best_bid_idx_];
        }
        if (best_ask_idx_ < NUM_LEVELS) {
            bbo.has_ask = true;
            bbo.ask_price = idx_to_price(best_ask_idx_);
            bbo.ask_qty = level_qty_[best_ask_idx_];
        }
        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const override {
        std::vector<Level> result;
        if (best_bid_idx_ < 0) return result;

        result.reserve(depth);
        int32_t idx = best_bid_idx_;
        while (idx >= 0 && static_cast<int>(result.size()) < depth) {
            if (level_qty_[idx] > 0) {
                result.push_back({idx_to_price(idx), level_qty_[idx]});
            }
            --idx;
        }
        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const override {
        std::vector<Level> result;
        if (best_ask_idx_ >= NUM_LEVELS) return result;

        result.reserve(depth);
        int32_t idx = best_ask_idx_;
        while (idx < NUM_LEVELS && static_cast<int>(result.size()) < depth) {
            if (level_qty_[idx] > 0) {
                result.push_back({idx_to_price(idx), level_qty_[idx]});
            }
            ++idx;
        }
        return result;
    }
};
