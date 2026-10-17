#pragma once

#include "order_book.hpp"
#include <vector>

using namespace std;

class OrderBookDirect : public OrderBook {
private:
    struct OrderSlot {
        uint64_t id{0};
        uint32_t tick{0};
        long qty{0};
        Side side{Side::Buy};
        bool active{false};
    };

    static constexpr size_t TABLE_CAPACITY = 1ULL << 21; // 2,097,152 slots
    static constexpr size_t TABLE_MASK = TABLE_CAPACITY - 1;

    long qty_[TOTAL_PRICE_TICKS]{};
    uint64_t bitmask_[157]{};

    uint32_t best_bid_tick_{0};
    uint32_t best_ask_tick_{0};

    vector<OrderSlot> table_;

    static inline size_t hash_id(uint64_t id) {
        return (id * 11400714819323198485ULL) & TABLE_MASK;
    }

    void remove_slot(size_t i) {
        size_t j = i;
        while (true) {
            table_[i].active = false;
            table_[i].id = 0;
            while (true) {
                j = (j + 1) & TABLE_MASK;
                if (!table_[j].active) {
                    return;
                }
                size_t k = hash_id(table_[j].id);
                bool inside = (i <= j) ? (i < k && k <= j) : (k > i || k <= j);
                if (!inside) {
                    break;
                }
            }
            table_[i] = table_[j];
            i = j;
        }
    }

    uint32_t find_prev_bid(uint32_t tick) const {
        if (tick <= MIN_PRICE_TICK) {
            return 0;
        }
        int curr_idx = static_cast<int>(tick - MIN_PRICE_TICK) - 1;
        int w = curr_idx >> 6;
        int b = curr_idx & 63;
        uint64_t mask = (b == 63) ? bitmask_[w] : (bitmask_[w] & ((1ULL << (b + 1)) - 1));
        if (mask != 0) {
            return MIN_PRICE_TICK + (static_cast<uint32_t>(w) << 6) + (63 - __builtin_clzll(mask));
        }
        for (int sw = w - 1; sw >= 0; --sw) {
            if (bitmask_[sw] != 0) {
                return MIN_PRICE_TICK + (static_cast<uint32_t>(sw) << 6) + (63 - __builtin_clzll(bitmask_[sw]));
            }
        }
        return 0;
    }

    uint32_t find_next_ask(uint32_t tick) const {
        if (tick < MIN_PRICE_TICK) {
            return 0;
        }
        int curr_idx = static_cast<int>(tick - MIN_PRICE_TICK) + 1;
        if (curr_idx >= static_cast<int>(TOTAL_PRICE_TICKS)) {
            return 0;
        }
        int w = curr_idx >> 6;
        int b = curr_idx & 63;
        uint64_t mask = bitmask_[w] & ~((1ULL << b) - 1);
        if (mask != 0) {
            return MIN_PRICE_TICK + (static_cast<uint32_t>(w) << 6) + __builtin_ctzll(mask);
        }
        for (int sw = w + 1; sw < 157; ++sw) {
            if (bitmask_[sw] != 0) {
                return MIN_PRICE_TICK + (static_cast<uint32_t>(sw) << 6) + __builtin_ctzll(bitmask_[sw]);
            }
        }
        return 0;
    }

    template <typename NextFn>
    vector<Level> extract_depth(uint32_t start_tick, int depth, NextFn next_fn) const {
        vector<Level> result;
        result.reserve(depth);
        for (uint32_t curr = start_tick; curr != 0 && static_cast<int>(result.size()) < depth; curr = next_fn(curr)) {
            result.push_back({tick_to_price(curr), qty_[curr - MIN_PRICE_TICK]});
        }
        return result;
    }

public:
    OrderBookDirect() : table_(TABLE_CAPACITY) {}

    void add_order(uint64_t id, Side side, double price, long qty) override {
        uint32_t tick = price_to_tick(price);

        size_t idx = hash_id(id);
        while (table_[idx].active) {
            idx = (idx + 1) & TABLE_MASK;
        }
        table_[idx] = OrderSlot{id, tick, qty, side, true};

        uint32_t pidx = tick - MIN_PRICE_TICK;
        qty_[pidx] += qty;
        bitmask_[pidx >> 6] |= (1ULL << (pidx & 63));

        if (side == Side::Buy) {
            if (best_bid_tick_ == 0 || tick > best_bid_tick_) {
                best_bid_tick_ = tick;
            }
        } else {
            if (best_ask_tick_ == 0 || tick < best_ask_tick_) {
                best_ask_tick_ = tick;
            }
        }
    }

    bool cancel_order(uint64_t id) override {
        size_t idx = hash_id(id);
        while (table_[idx].active) {
            if (table_[idx].id == id) {
                OrderSlot ord = table_[idx];
                remove_slot(idx);

                uint32_t pidx = ord.tick - MIN_PRICE_TICK;
                qty_[pidx] -= ord.qty;
                if (qty_[pidx] <= 0) {
                    qty_[pidx] = 0;
                    bitmask_[pidx >> 6] &= ~(1ULL << (pidx & 63));

                    if (ord.side == Side::Buy) {
                        if (ord.tick == best_bid_tick_) {
                            best_bid_tick_ = find_prev_bid(ord.tick);
                        }
                    } else {
                        if (ord.tick == best_ask_tick_) {
                            best_ask_tick_ = find_next_ask(ord.tick);
                        }
                    }
                }
                return true;
            }
            idx = (idx + 1) & TABLE_MASK;
        }
        return false;
    }

    BBO get_bbo() const override {
        BBO bbo{};
        if (best_bid_tick_ != 0) {
            bbo.has_bid = true;
            bbo.bid_price = tick_to_price(best_bid_tick_);
            bbo.bid_qty = qty_[best_bid_tick_ - MIN_PRICE_TICK];
        }
        if (best_ask_tick_ != 0) {
            bbo.has_ask = true;
            bbo.ask_price = tick_to_price(best_ask_tick_);
            bbo.ask_qty = qty_[best_ask_tick_ - MIN_PRICE_TICK];
        }
        return bbo;
    }

    vector<Level> get_bids(int depth = 10) const override {
        return extract_depth(best_bid_tick_, depth, [this](uint32_t t) { return find_prev_bid(t); });
    }

    vector<Level> get_asks(int depth = 10) const override {
        return extract_depth(best_ask_tick_, depth, [this](uint32_t t) { return find_next_ask(t); });
    }
};
