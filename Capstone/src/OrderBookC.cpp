#include "OrderBookC.hpp"
#include <algorithm>

OrderBookC::OrderBookC() {
    std::memset(quantities_, 0, sizeof(quantities_));
    std::memset(bitmap_, 0, sizeof(bitmap_));
    orders_.reserve(65536);
}

int OrderBookC::find_prev_active_bid(int from_tick) const {
    if (from_tick < 0) return -1;
    if (from_tick >= Config::NUM_TICKS) from_tick = Config::NUM_TICKS - 1;

    int w = from_tick / 64;
    int bit = from_tick % 64;

    uint64_t mask = (bit == 63) ? ~0ULL : ((1ULL << (bit + 1)) - 1ULL);
    uint64_t val = bitmap_[w] & mask;
    if (val != 0) {
        int highest_bit = 63 - __builtin_clzll(val);
        return w * 64 + highest_bit;
    }

    for (int i = w - 1; i >= 0; --i) {
        if (bitmap_[i] != 0) {
            int highest_bit = 63 - __builtin_clzll(bitmap_[i]);
            return i * 64 + highest_bit;
        }
    }

    return -1;
}

int OrderBookC::find_next_active_ask(int from_tick) const {
    if (from_tick >= Config::NUM_TICKS) return -1;
    if (from_tick < 0) from_tick = 0;

    int w = from_tick / 64;
    int bit = from_tick % 64;

    uint64_t mask = (bit == 0) ? ~0ULL : ~((1ULL << bit) - 1ULL);
    uint64_t val = bitmap_[w] & mask;
    if (val != 0) {
        int lowest_bit = __builtin_ctzll(val);
        int tick = w * 64 + lowest_bit;
        return (tick < Config::NUM_TICKS) ? tick : -1;
    }

    for (int i = w + 1; i < NUM_WORDS; ++i) {
        if (bitmap_[i] != 0) {
            int lowest_bit = __builtin_ctzll(bitmap_[i]);
            int tick = i * 64 + lowest_bit;
            return (tick < Config::NUM_TICKS) ? tick : -1;
        }
    }

    return -1;
}

void OrderBookC::add_order(uint64_t id, Side side, double price, long qty) {
    int64_t cents = price_to_cents(price);
    int tick = cents_to_tick(cents);
    if (tick < 0 || tick >= Config::NUM_TICKS) return;

    orders_[id] = OrderRecord{id, side, price, qty, tick};

    if (quantities_[tick] == 0) {
        bitmap_[tick / 64] |= (1ULL << (tick % 64));
        if (side == Side::Buy) {
            ++bid_level_count_;
        } else {
            ++ask_level_count_;
        }
    }
    quantities_[tick] += qty;

    if (side == Side::Buy) {
        if (best_bid_tick_ < 0 || tick > best_bid_tick_) {
            best_bid_tick_ = tick;
        }
    } else {
        if (best_ask_tick_ < 0 || tick < best_ask_tick_) {
            best_ask_tick_ = tick;
        }
    }
}

bool OrderBookC::cancel_order(uint64_t id) {
    auto oit = orders_.find(id);
    if (oit == orders_.end()) {
        return false;
    }

    const OrderRecord& ord = oit->second;
    int tick = ord.tick;

    quantities_[tick] -= ord.qty;
    if (quantities_[tick] <= 0) {
        quantities_[tick] = 0;
        bitmap_[tick / 64] &= ~(1ULL << (tick % 64));

        if (ord.side == Side::Buy) {
            if (bid_level_count_ > 0) --bid_level_count_;
            if (tick == best_bid_tick_) {
                best_bid_tick_ = find_prev_active_bid(best_bid_tick_ - 1);
            }
        } else {
            if (ask_level_count_ > 0) --ask_level_count_;
            if (tick == best_ask_tick_) {
                best_ask_tick_ = find_next_active_ask(best_ask_tick_ + 1);
            }
        }
    }

    orders_.erase(oit);
    return true;
}

BBO OrderBookC::get_bbo() const {
    BBO bbo;
    if (best_bid_tick_ >= 0) {
        bbo.has_bid = true;
        bbo.bid_price = tick_to_price(best_bid_tick_);
        bbo.bid_qty = quantities_[best_bid_tick_];
    }
    if (best_ask_tick_ >= 0) {
        bbo.has_ask = true;
        bbo.ask_price = tick_to_price(best_ask_tick_);
        bbo.ask_qty = quantities_[best_ask_tick_];
    }
    return bbo;
}

std::vector<Level> OrderBookC::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_bid_tick_ < 0) return result;
    result.reserve(std::min<size_t>(depth, bid_level_count_));

    int curr = best_bid_tick_;
    int count = 0;
    while (curr >= 0 && count < depth) {
        result.push_back(Level{tick_to_price(curr), quantities_[curr]});
        ++count;
        if (count >= depth) break;
        curr = find_prev_active_bid(curr - 1);
    }
    return result;
}

std::vector<Level> OrderBookC::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0 || best_ask_tick_ < 0) return result;
    result.reserve(std::min<size_t>(depth, ask_level_count_));

    int curr = best_ask_tick_;
    int count = 0;
    while (curr >= 0 && count < depth) {
        result.push_back(Level{tick_to_price(curr), quantities_[curr]});
        ++count;
        if (count >= depth) break;
        curr = find_next_active_ask(curr + 1);
    }
    return result;
}
