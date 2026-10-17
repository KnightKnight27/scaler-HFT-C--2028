#pragma once

#include "utils.hpp"
#include <array>
#include <vector>

class OrderBookC {
private:
    using Tick = int32_t;

    struct DirectOrderSlot {
        long qty{0};
        Tick tick{0};
        Side side{Side::Buy};
        bool active{false};
    };

    // Co-locate hot BBO state on a single 64-byte cache line
    alignas(64) int32_t best_bid_idx{-1};
    int32_t best_ask_idx{TICKS_PER_SIDE};

    // Fixed-size arrays for the 5,000 possible bid ticks and 5,000 possible ask ticks
    alignas(64) std::array<long, TICKS_PER_SIDE> bid_qty{};
    alignas(64) std::array<long, TICKS_PER_SIDE> ask_qty{};

    // Pre-allocated flat direct-index array for O(1) order lookup by ID
    std::vector<DirectOrderSlot> order_table;

public:
    explicit OrderBookC(size_t max_order_id = 1'000'005)
        : order_table(max_order_id) {}

    void add_order(uint64_t id, Side side, double price, long qty) {
        Tick tick = price_to_tick(price);

        if (id >= order_table.size()) [[unlikely]] {
            order_table.resize(id + 1024);
        }

        order_table[id] = {qty, tick, side, true};

        if (side == Side::Buy) {
            int32_t idx = tick - MIN_BID_TICK;
            bid_qty[idx] += qty;
            if (idx > best_bid_idx) {
                best_bid_idx = idx;
            }
        } else {
            int32_t idx = tick - MIN_ASK_TICK;
            ask_qty[idx] += qty;
            if (idx < best_ask_idx) {
                best_ask_idx = idx;
            }
        }
    }

    bool cancel_order(uint64_t id) {
        if (id >= order_table.size() || !order_table[id].active) [[unlikely]] {
            return false;
        }

        DirectOrderSlot& slot = order_table[id];
        slot.active = false;

        if (slot.side == Side::Buy) {
            int32_t idx = slot.tick - MIN_BID_TICK;
            bid_qty[idx] -= slot.qty;
            if (idx == best_bid_idx && bid_qty[idx] <= 0) {
                while (best_bid_idx >= 0 && bid_qty[best_bid_idx] <= 0) {
                    --best_bid_idx;
                }
            }
        } else {
            int32_t idx = slot.tick - MIN_ASK_TICK;
            ask_qty[idx] -= slot.qty;
            if (idx == best_ask_idx && ask_qty[idx] <= 0) {
                while (best_ask_idx < TICKS_PER_SIDE && ask_qty[best_ask_idx] <= 0) {
                    ++best_ask_idx;
                }
            }
        }

        return true;
    }

    BBO get_bbo() const {
        BBO bbo{};
        if (best_bid_idx >= 0) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(best_bid_idx + MIN_BID_TICK);
            bbo.bid_qty = bid_qty[best_bid_idx];
        } else {
            bbo.has_bid = false;
        }

        if (best_ask_idx < TICKS_PER_SIDE) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(best_ask_idx + MIN_ASK_TICK);
            bbo.ask_qty = ask_qty[best_ask_idx];
        } else {
            bbo.has_ask = false;
        }

        return bbo;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0 || best_bid_idx < 0) return result;
        result.reserve(depth);

        for (int32_t idx = best_bid_idx; idx >= 0 && depth > 0; --idx) {
            if (bid_qty[idx] > 0) {
                result.push_back({tick_to_price(idx + MIN_BID_TICK), bid_qty[idx]});
                --depth;
            }
        }

        return result;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> result;
        if (depth <= 0 || best_ask_idx >= TICKS_PER_SIDE) return result;
        result.reserve(depth);

        for (int32_t idx = best_ask_idx; idx < TICKS_PER_SIDE && depth > 0; ++idx) {
            if (ask_qty[idx] > 0) {
                result.push_back({tick_to_price(idx + MIN_ASK_TICK), ask_qty[idx]});
                --depth;
            }
        }

        return result;
    }
};