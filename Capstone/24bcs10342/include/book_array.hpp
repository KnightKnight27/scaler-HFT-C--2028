// Implementation C (own design): price-indexed flat array + cached best + pooled orders
// + custom open-addressing hash table.
//   levels : long qty_[10001], index = tick - 5000 (bids and asks share one array;
//            side is implied by the tick, ticks <10000 are bids, >10000 are asks)
//   orders : slot pool (vector<Slot> + free list), located via IdTable (id -> slot)
//   best   : cached best_bid_/best_ask_ ticks, updated on add (O(1)); rescanned only
//            when the best level is emptied by a cancel
#pragma once
#include <vector>
#include "types.hpp"

// Linear-probing hash table with backward-shift deletion (no tombstones).
class IdTable {
public:
    static constexpr size_t npos = ~size_t(0);
    IdTable() { init(size_t(1) << 16); }

    size_t find(uint64_t key) const {
        size_t i = home(key);
        while (t_[i].used) {
            if (t_[i].key == key) return i;
            i = (i + 1) & mask_;
        }
        return npos;
    }
    bool insert(uint64_t key, uint32_t val) {
        if ((size_ + 1) * 2 > t_.size()) grow();
        size_t i = home(key);
        while (t_[i].used) {
            if (t_[i].key == key) return false;
            i = (i + 1) & mask_;
        }
        t_[i] = {key, val, true};
        ++size_;
        return true;
    }
    uint32_t value(size_t i) const { return t_[i].val; }

    void erase_at(size_t i) {
        size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (!t_[j].used) break;
            size_t k = home(t_[j].key);
            bool k_in_ij = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
            if (k_in_ij) continue;  // entry j is still reachable from its home slot
            t_[i] = t_[j];
            i = j;
        }
        t_[i].used = false;
        --size_;
    }

private:
    struct Entry { uint64_t key; uint32_t val; bool used; };

    void init(size_t cap) {
        t_.assign(cap, Entry{0, 0, false});
        mask_ = cap - 1;
        unsigned lg = 0;
        while ((size_t(1) << lg) < cap) ++lg;
        shift_ = 64 - lg;
        size_ = 0;
    }
    void grow() {
        std::vector<Entry> old = std::move(t_);
        init(old.size() * 2);
        for (const Entry& e : old)
            if (e.used) insert(e.key, e.val);
    }
    size_t home(uint64_t k) const { return static_cast<size_t>((k * 0x9E3779B97F4A7C15ull) >> shift_); }

    std::vector<Entry> t_;
    size_t mask_ = 0, size_ = 0;
    unsigned shift_ = 0;
};

class OrderBookArray {
    static constexpr int kLevels = kMaxAskTick - kMinBidTick + 1;
    struct Slot { int tick; long qty; Side side; };

public:
    static const char* name() { return "C (price-indexed array + cached BBO + pooled orders + open-addressing hash)"; }

    OrderBookArray() : qty_(kLevels, 0) { slots_.reserve(1 << 16); }

    bool add_order(uint64_t id, Side side, int tick, long qty) {
        if (qty <= 0 || !valid_tick(side, tick)) return false;
        uint32_t s;
        if (!free_.empty()) { s = free_.back(); free_.pop_back(); slots_[s] = {tick, qty, side}; }
        else { s = static_cast<uint32_t>(slots_.size()); slots_.push_back({tick, qty, side}); }
        if (!ids_.insert(id, s)) {  // duplicate id: give the slot back
            free_.push_back(s);
            return false;
        }
        qty_[tick - kMinBidTick] += qty;
        if (side == Side::Buy) { if (tick > best_bid_) best_bid_ = tick; }
        else { if (best_ask_ < 0 || tick < best_ask_) best_ask_ = tick; }
        return true;
    }

    bool cancel_order(uint64_t id) {
        size_t pos = ids_.find(id);
        if (pos == IdTable::npos) return false;
        uint32_t s = ids_.value(pos);
        ids_.erase_at(pos);
        const Slot o = slots_[s];
        free_.push_back(s);
        long& q = qty_[o.tick - kMinBidTick];
        q -= o.qty;
        if (q == 0) {  // level emptied: only rescan if it was the best
            if (o.side == Side::Buy && o.tick == best_bid_) {
                int t = o.tick - 1;
                while (t >= kMinBidTick && qty_[t - kMinBidTick] == 0) --t;
                best_bid_ = (t >= kMinBidTick) ? t : -1;
            } else if (o.side == Side::Sell && o.tick == best_ask_) {
                int t = o.tick + 1;
                while (t <= kMaxAskTick && qty_[t - kMinBidTick] == 0) ++t;
                best_ask_ = (t <= kMaxAskTick) ? t : -1;
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (best_bid_ >= 0) { b.has_bid = true; b.bid_price = best_bid_; b.bid_qty = qty_[best_bid_ - kMinBidTick]; }
        if (best_ask_ >= 0) { b.has_ask = true; b.ask_price = best_ask_; b.ask_qty = qty_[best_ask_ - kMinBidTick]; }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        if (best_bid_ < 0) return out;
        for (int t = best_bid_; t >= kMinBidTick && static_cast<int>(out.size()) < depth; --t)
            if (long q = qty_[t - kMinBidTick]) out.push_back({t, q});
        return out;
    }
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        if (best_ask_ < 0) return out;
        for (int t = best_ask_; t <= kMaxAskTick && static_cast<int>(out.size()) < depth; ++t)
            if (long q = qty_[t - kMinBidTick]) out.push_back({t, q});
        return out;
    }

private:
    std::vector<long> qty_;
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    IdTable ids_;
    int best_bid_ = -1;
    int best_ask_ = -1;
};
