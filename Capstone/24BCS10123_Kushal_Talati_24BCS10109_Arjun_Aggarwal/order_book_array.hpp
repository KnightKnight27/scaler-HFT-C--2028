#pragma once
// impl C: flat arrays indexed by tick, occupancy bitmap for best lookup, slab of orders by id
#include "order_book_api.hpp"
#include <array>
#include <unordered_map>

struct OrderBookArray {
    static constexpr int N = LEVELS_PER_SIDE;
    static constexpr int W = (N + 63) / 64;  // 79 words

    // both sides map ticks to idx so that idx 0 is the BEST price.
    // bids: idx = 9999 - tick   asks: idx = tick - 10001
    // so "find best" is always "lowest set bit" on both sides.
    struct SideArr {
        std::array<long, N> qty{};
        std::array<uint64_t, W> bits{};
        int best = -1;  // idx of best level, -1 if side empty
        int32_t base;
        int32_t dir;    // tick = base + dir * idx
        SideArr(int32_t b, int32_t d) : base(b), dir(d) {}

        int to_idx(int32_t tick) const { return (tick - base) * dir; }
        int32_t to_tick(int idx) const { return base + dir * idx; }

        // first set bit at index >= from, or -1
        int find_from(int from) const {
            if (from >= N) return -1;
            int w = from >> 6;
            uint64_t word = bits[w] & (~0ULL << (from & 63));
            while (word == 0) {
                if (++w == W) return -1;
                word = bits[w];
            }
            return (w << 6) + __builtin_ctzll(word);
        }

        void add(int idx, long q) {
            if (qty[idx] == 0) {
                bits[idx >> 6] |= 1ULL << (idx & 63);
                if (best < 0 || idx < best) best = idx;
            }
            qty[idx] += q;
        }

        void sub(int idx, long q) {
            qty[idx] -= q;
            if (qty[idx] == 0) {
                bits[idx >> 6] &= ~(1ULL << (idx & 63));
                if (idx == best) best = find_from(idx + 1);  // best just died, scan forward for the next one
            }
        }

        std::vector<Level> walk(int depth) const {
            std::vector<Level> out;
            if (depth <= 0) return out;
            size_t want = static_cast<size_t>(depth);
            out.reserve(want < 64 ? want : 64);  // first version forgot this and lost to A on depth, see report
            int idx = best;
            while (idx >= 0 && out.size() < want) {
                out.push_back(Level{::to_price(to_tick(idx)), qty[idx]});
                idx = find_from(idx + 1);
            }
            return out;
        }
    };

    SideArr bids{BID_MAX_TICK, -1};
    SideArr asks{ASK_MIN_TICK, +1};

    // ids are dense (1,2,3,...) so a vector indexed by id works, see README.
    // anything >= slab size falls back to the hash map so correctness never depends on it.
    std::vector<OrderRec> slab;
    std::unordered_map<uint64_t, OrderRec> overflow;

    explicit OrderBookArray(size_t max_orders = 1u << 20) : slab(max_orders, OrderRec{0, 0, Side::Buy, false}) {}

    bool add_order(uint64_t id, Side side, double price, long qty) {
        int32_t tick = ::to_tick(price);
        if (qty <= 0 || !tick_in_band(side, tick)) return false;
        OrderRec rec{tick, static_cast<int32_t>(qty), side, true};
        if (id < slab.size()) {
            if (slab[id].live) return false;
            slab[id] = rec;
        } else {
            if (!overflow.emplace(id, rec).second) return false;
        }
        if (side == Side::Buy) bids.add(bids.to_idx(tick), qty);
        else asks.add(asks.to_idx(tick), qty);
        return true;
    }

    bool cancel_order(uint64_t id) {
        OrderRec o;
        if (id < slab.size()) {
            if (!slab[id].live) return false;  // unknown or already cancelled, O(1)
            o = slab[id];
            slab[id].live = false;
        } else {
            auto it = overflow.find(id);
            if (it == overflow.end()) return false;
            o = it->second;
            overflow.erase(it);
        }
        if (o.side == Side::Buy) bids.sub(bids.to_idx(o.tick), o.qty);
        else asks.sub(asks.to_idx(o.tick), o.qty);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        if (bids.best >= 0) {
            b.has_bid = true;
            b.bid_price = ::to_price(bids.to_tick(bids.best));
            b.bid_qty = bids.qty[bids.best];
        }
        if (asks.best >= 0) {
            b.has_ask = true;
            b.ask_price = ::to_price(asks.to_tick(asks.best));
            b.ask_qty = asks.qty[asks.best];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const { return bids.walk(depth); }
    std::vector<Level> get_asks(int depth = 10) const { return asks.walk(depth); }
};
