#include "OrderBookC.hpp"
#include <algorithm>
#include <stdexcept>

OrderBookC::OrderBookC(size_t initial_order_capacity)
    : best_bid_tick_(-1),
      best_ask_tick_(10001),
      order_count_(0) {
    std::memset(price_qty_, 0, sizeof(price_qty_));
    std::memset(active_mask_, 0, sizeof(active_mask_));

    // Ensure capacity is a power of 2
    size_t cap = 1;
    while (cap < initial_order_capacity) {
        cap <<= 1;
    }
    capacity_ = cap;
    mask_ = capacity_ - 1;
    order_table_ = new OrderSlot[capacity_]();
}

OrderBookC::~OrderBookC() {
    delete[] order_table_;
}

void OrderBookC::rehash(size_t new_capacity) {
    size_t old_cap = capacity_;
    OrderSlot* old_table = order_table_;

    capacity_ = new_capacity;
    mask_ = capacity_ - 1;
    order_table_ = new OrderSlot[capacity_]();
    order_count_ = 0;

    for (size_t i = 0; i < old_cap; ++i) {
        if (old_table[i].occupied) {
            size_t idx = hash_id(old_table[i].id) & mask_;
            while (order_table_[idx].occupied) {
                idx = (idx + 1) & mask_;
            }
            order_table_[idx] = old_table[i];
            ++order_count_;
        }
    }
    delete[] old_table;
}

int OrderBookC::find_prev_active_bid(int from_tick) const {
    if (from_tick <= 0) return -1;
    int target = from_tick - 1;
    int word_idx = target / 64;
    int bit_idx = target % 64;

    uint64_t mask = (bit_idx == 63) ? ~0ULL : ((1ULL << (bit_idx + 1)) - 1);
    uint64_t w = active_mask_[word_idx] & mask;
    if (w != 0) {
        int leading_zeros = __builtin_clzll(w);
        return word_idx * 64 + (63 - leading_zeros);
    }

    for (int i = word_idx - 1; i >= 0; --i) {
        if (active_mask_[i] != 0) {
            int leading_zeros = __builtin_clzll(active_mask_[i]);
            return i * 64 + (63 - leading_zeros);
        }
    }
    return -1;
}

int OrderBookC::find_next_active_ask(int from_tick) const {
    if (from_tick >= static_cast<int>(NUM_TICKS) - 1) return 10001;
    int target = from_tick + 1;
    int word_idx = target / 64;
    int bit_idx = target % 64;

    uint64_t mask = ~0ULL << bit_idx;
    uint64_t w = active_mask_[word_idx] & mask;
    if (w != 0) {
        int trailing_zeros = __builtin_ctzll(w);
        return word_idx * 64 + trailing_zeros;
    }

    for (size_t i = word_idx + 1; i < NUM_WORDS; ++i) {
        if (active_mask_[i] != 0) {
            int trailing_zeros = __builtin_ctzll(active_mask_[i]);
            return static_cast<int>(i * 64 + trailing_zeros);
        }
    }
    return 10001;
}

void OrderBookC::add_order(uint64_t id, Side side, double price, long qty) {
    if (order_count_ * 10 >= capacity_ * 7) {
        rehash(capacity_ * 2);
    }

    size_t idx = hash_id(id) & mask_;
    while (order_table_[idx].occupied && order_table_[idx].id != id) {
        idx = (idx + 1) & mask_;
    }

    if (!order_table_[idx].occupied) {
        ++order_count_;
    }

    int tick = price_to_tick(price);
    if (tick < 0) tick = 0;
    if (tick >= static_cast<int>(NUM_TICKS)) tick = static_cast<int>(NUM_TICKS) - 1;

    order_table_[idx].id = id;
    order_table_[idx].qty = qty;
    order_table_[idx].tick = static_cast<uint16_t>(tick);
    order_table_[idx].side = static_cast<uint8_t>(side);
    order_table_[idx].occupied = true;

    if (price_qty_[tick] == 0) {
        set_active(tick);
    }
    price_qty_[tick] += qty;

    if (side == Side::Buy) {
        if (tick > best_bid_tick_) {
            best_bid_tick_ = tick;
        }
    } else {
        if (tick < best_ask_tick_) {
            best_ask_tick_ = tick;
        }
    }
}

bool OrderBookC::cancel_order(uint64_t id) {
    size_t idx = hash_id(id) & mask_;
    while (order_table_[idx].occupied) {
        if (order_table_[idx].id == id) {
            int tick = order_table_[idx].tick;
            Side side = static_cast<Side>(order_table_[idx].side);
            long qty = order_table_[idx].qty;

            price_qty_[tick] -= qty;
            if (price_qty_[tick] <= 0) {
                price_qty_[tick] = 0;
                clear_active(tick);
                if (side == Side::Buy && tick == best_bid_tick_) {
                    best_bid_tick_ = find_prev_active_bid(tick);
                } else if (side == Side::Sell && tick == best_ask_tick_) {
                    best_ask_tick_ = find_next_active_ask(tick);
                }
            }

            // Backward shift deletion to avoid tombstones
            size_t curr = idx;
            size_t next = (curr + 1) & mask_;
            while (order_table_[next].occupied) {
                size_t natural = hash_id(order_table_[next].id) & mask_;
                bool can_shift = false;
                if (next >= curr) {
                    can_shift = (natural <= curr || natural > next);
                } else {
                    can_shift = (natural <= curr && natural > next);
                }
                if (can_shift) {
                    order_table_[curr] = order_table_[next];
                    curr = next;
                }
                next = (next + 1) & mask_;
            }
            order_table_[curr].occupied = false;
            order_table_[curr].id = 0;
            --order_count_;
            return true;
        }
        idx = (idx + 1) & mask_;
    }
    return false;
}

BBO OrderBookC::get_bbo() const {
    BBO bbo{};
    if (best_bid_tick_ >= 0) {
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(best_bid_tick_);
        bbo.bid_qty = price_qty_[best_bid_tick_];
    } else {
        bbo.has_bid = false;
        bbo.bid_price = 0.0;
        bbo.bid_qty = 0;
    }

    if (best_ask_tick_ <= 10000) {
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(best_ask_tick_);
        bbo.ask_qty = price_qty_[best_ask_tick_];
    } else {
        bbo.has_ask = false;
        bbo.ask_price = 0.0;
        bbo.ask_qty = 0;
    }
    return bbo;
}

std::vector<Level> OrderBookC::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_bid_tick_ < 0) return result;
    result.reserve(depth);

    int curr = best_bid_tick_;
    while (curr >= 0 && static_cast<int>(result.size()) < depth) {
        result.push_back({tick_to_price(curr), price_qty_[curr]});
        curr = find_prev_active_bid(curr);
    }
    return result;
}

std::vector<Level> OrderBookC::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_ask_tick_ > 10000) return result;
    result.reserve(depth);

    int curr = best_ask_tick_;
    while (curr <= 10000 && static_cast<int>(result.size()) < depth) {
        result.push_back({tick_to_price(curr), price_qty_[curr]});
        curr = find_next_active_ask(curr);
    }
    return result;
}
