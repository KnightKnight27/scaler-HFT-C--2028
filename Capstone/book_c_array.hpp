// Rohan Ranjan-10428-Capstone project
//
// Implementation C (my own design): flat price array + bitmap + orders indexed by id
//
// orders      : std::vector indexed directly by order id -> {tick, qty}
//               (ids come in increasing and are never reused, so the id itself is the index.
//               ids bigger than kDirectIds go to a small unordered_map fallback)
// price levels: one array of 10001 totals, index = tick - 5000. no tree, no hash.
// best price  : best bid / best ask index is cached, so get_bbo is two array reads.
//               when the best level empties, a bitmap (1 bit per tick, 157 words)
//               finds the next non-empty level 64 ticks at a time.
// cancel      : orders[id] gives the tick straight away, subtract from the array

#pragma once

#include <bit>
#include <unordered_map>

#include "orderbook.hpp"

class BookC {
    struct Slot {
        long qty  = 0;
        int  tick = 0; // 0 = no live order with this id
    };

    static constexpr uint64_t kDirectIds = 1u << 24; // up to ~16M ids in the vector (256 MB max)
    static constexpr int      kWords     = (kNumTicks + 63) / 64;

    std::vector<Slot>              by_id_;   // id -> order
    std::unordered_map<uint64_t, Slot> big_ids_; // only for ids >= kDirectIds
    std::vector<long>              qty_;     // total qty per tick
    std::vector<uint64_t>          bits_;    // bit i set <=> qty_[i] > 0
    int best_bid_ = -1;                      // index into qty_, -1 = no bids
    int best_ask_ = -1;                      // index into qty_, -1 = no asks

    static int idx(int tick) { return tick - kMinBidTick; }

    void set_bit(int i)   { bits_[i >> 6] |=  (1ULL << (i & 63)); }
    void clear_bit(int i) { bits_[i >> 6] &= ~(1ULL << (i & 63)); }

    // highest non-empty index <= i, or -1
    int prev_set(int i) const {
        if (i < 0) return -1;
        int w = i >> 6;
        uint64_t m = bits_[w] & (~0ULL >> (63 - (i & 63))); // keep bits 0..i
        while (true) {
            if (m) return (w << 6) + 63 - std::countl_zero(m);
            if (--w < 0) return -1;
            m = bits_[w];
        }
    }

    // lowest non-empty index >= i, or -1
    int next_set(int i) const {
        if (i >= kNumTicks) return -1;
        int w = i >> 6;
        uint64_t m = bits_[w] & (~0ULL << (i & 63)); // keep bits i..63
        while (true) {
            if (m) return (w << 6) + std::countr_zero(m);
            if (++w >= kWords) return -1;
            m = bits_[w];
        }
    }

    Slot* find_slot(uint64_t id) {
        if (id < kDirectIds) return id < by_id_.size() ? &by_id_[id] : nullptr;
        auto it = big_ids_.find(id);
        return it == big_ids_.end() ? nullptr : &it->second;
    }

    Slot& make_slot(uint64_t id) {
        if (id >= kDirectIds) return big_ids_[id];
        if (id >= by_id_.size()) by_id_.resize(std::max<size_t>(id + 1, by_id_.size() * 2));
        return by_id_[id];
    }

public:
    static const char* name() { return "C (price array + bitmap + id-indexed orders)"; }

    BookC() : qty_(kNumTicks, 0), bits_(kWords, 0) { by_id_.resize(1024); }

    void add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!parse_order(side, price, qty, tick)) return;
        Slot& s = make_slot(id);
        if (s.tick != 0) return; // id already live
        s.tick = tick;
        s.qty  = qty;

        int i = idx(tick);
        if (qty_[i] == 0) set_bit(i);
        qty_[i] += qty;

        if (side == Side::Buy) { if (i > best_bid_) best_bid_ = i; }
        else if (best_ask_ < 0 || i < best_ask_) best_ask_ = i;
    }

    bool cancel_order(uint64_t id) {
        Slot* s = find_slot(id);
        if (!s || s->tick == 0) return false; // unknown or already cancelled
        int i = idx(s->tick);
        qty_[i] -= s->qty;
        if (id >= kDirectIds) big_ids_.erase(id);
        else                  s->tick = 0;

        if (qty_[i] == 0) {
            clear_bit(i);
            // bids live below index 5000 and asks above it, so searching down from a bid
            // only ever finds bids, and searching up from an ask only finds asks
            if (i == best_bid_)      best_bid_ = prev_set(i - 1);
            else if (i == best_ask_) best_ask_ = next_set(i + 1);
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (best_bid_ >= 0) {
            b.has_bid   = true;
            b.bid_price = to_price(best_bid_ + kMinBidTick);
            b.bid_qty   = qty_[best_bid_];
        }
        if (best_ask_ >= 0) {
            b.has_ask   = true;
            b.ask_price = to_price(best_ask_ + kMinBidTick);
            b.ask_qty   = qty_[best_ask_];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int i = best_bid_; i >= 0 && (int)out.size() < depth; i = prev_set(i - 1))
            out.push_back({to_price(i + kMinBidTick), qty_[i]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int i = best_ask_; i >= 0 && (int)out.size() < depth; i = next_set(i + 1))
            out.push_back({to_price(i + kMinBidTick), qty_[i]});
        return out;
    }
};
