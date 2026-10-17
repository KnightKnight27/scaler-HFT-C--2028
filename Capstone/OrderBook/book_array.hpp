#pragma once
#include <algorithm>
#include <unordered_map>
#include "common.hpp"

constexpr int LEVELS = MAX_TICK - MIN_TICK + 1;

// two-level bitmap over the 10001 price slots: bit i of w[] = level i non-empty,
// bit k of top[] = word w[k] non-empty. lets us jump to the next level with clz/ctz
struct LevelBits {
    static constexpr int W = (LEVELS + 63) / 64;
    static constexpr int TW = (W + 63) / 64;
    uint64_t w[W] = {};
    uint64_t top[TW] = {};

    void set(int i) {
        w[i >> 6] |= 1ull << (i & 63);
        top[i >> 12] |= 1ull << ((i >> 6) & 63);
    }

    void clear(int i) {
        int k = i >> 6;
        w[k] &= ~(1ull << (i & 63));
        if (!w[k]) top[k >> 6] &= ~(1ull << (k & 63));
    }

    // highest set bit <= i, or -1
    int prev(int i) const {
        if (i < 0) return -1;
        int k = i >> 6;
        uint64_t m = w[k] & (~0ull >> (63 - (i & 63)));
        if (m) return (k << 6) + 63 - __builtin_clzll(m);
        if (--k < 0) return -1;
        int tk = k >> 6;
        uint64_t tm = top[tk] & (~0ull >> (63 - (k & 63)));
        while (!tm) {
            if (--tk < 0) return -1;
            tm = top[tk];
        }
        int wk = (tk << 6) + 63 - __builtin_clzll(tm);
        return (wk << 6) + 63 - __builtin_clzll(w[wk]);
    }

    // lowest set bit >= i, or -1
    int next(int i) const {
        if (i >= LEVELS) return -1;
        int k = i >> 6;
        uint64_t m = w[k] & (~0ull << (i & 63));
        if (m) return (k << 6) + __builtin_ctzll(m);
        if (++k >= W) return -1;
        int tk = k >> 6;
        uint64_t tm = top[tk] & (~0ull << (k & 63));
        while (!tm) {
            if (++tk >= TW) return -1;
            tm = top[tk];
        }
        int wk = (tk << 6) + __builtin_ctzll(tm);
        return (wk << 6) + __builtin_ctzll(w[wk]);
    }
};

// Implementation C: price-indexed array + bitmap + id-indexed order vector
class ArrayBook {
    static constexpr uint64_t DENSE_IDS = 1 << 24;

    long qty[LEVELS] = {};
    LevelBits bid_bits, ask_bits;
    int best_bid = -1, best_ask = -1;

    // orders[id] for small ids (qty == 0 means not live), hash map for anything bigger
    std::vector<Order> orders;
    std::unordered_map<uint64_t, Order> far_orders;

    Order* find(uint64_t id) {
        if (id < DENSE_IDS) return id < orders.size() && orders[id].qty ? &orders[id] : nullptr;
        auto it = far_orders.find(id);
        return it == far_orders.end() ? nullptr : &it->second;
    }

    void erase(uint64_t id) {
        if (id < DENSE_IDS) orders[id].qty = 0;
        else far_orders.erase(id);
    }

    template <class F>
    std::vector<Level> walk(int start, int depth, F step) const {
        std::vector<Level> out;
        out.reserve(depth > 0 ? depth : 0);
        for (int i = start; i >= 0 && (int)out.size() < depth; i = step(i))
            out.push_back({to_price(i + MIN_TICK), qty[i]});
        return out;
    }

public:
    ArrayBook() { orders.reserve(1 << 20); }

    void add_order(uint64_t id, Side side, double price, long q) {
        int t = to_tick(price);
        if (!valid(side, t) || q <= 0 || find(id)) return;
        if (id < DENSE_IDS) {
            if (id >= orders.size()) orders.resize(std::max<size_t>(id + 1, orders.size() * 2), Order{0, 0, Side::Buy});
            orders[id] = {t, q, side};
        } else {
            far_orders[id] = {t, q, side};
        }

        int i = t - MIN_TICK;
        if (qty[i] == 0) (side == Side::Buy ? bid_bits : ask_bits).set(i);
        qty[i] += q;
        if (side == Side::Buy && i > best_bid) best_bid = i;
        if (side == Side::Sell && (best_ask < 0 || i < best_ask)) best_ask = i;
    }

    bool cancel_order(uint64_t id) {
        Order* o = find(id);
        if (!o) return false;
        int i = o->tick - MIN_TICK;
        Side side = o->side;
        qty[i] -= o->qty;
        erase(id);
        if (qty[i] == 0) {
            if (side == Side::Buy) {
                bid_bits.clear(i);
                if (i == best_bid) best_bid = bid_bits.prev(i);
            } else {
                ask_bits.clear(i);
                if (i == best_ask) best_ask = ask_bits.next(i);
            }
        }
        return true;
    }

    BBO get_bbo() const {
        BBO b{false, 0, 0, false, 0, 0};
        if (best_bid >= 0) {
            b.has_bid = true;
            b.bid_price = to_price(best_bid + MIN_TICK);
            b.bid_qty = qty[best_bid];
        }
        if (best_ask >= 0) {
            b.has_ask = true;
            b.ask_price = to_price(best_ask + MIN_TICK);
            b.ask_qty = qty[best_ask];
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        return walk(best_bid, depth, [this](int i) { return bid_bits.prev(i - 1); });
    }
    std::vector<Level> get_asks(int depth = 10) const {
        return walk(best_ask, depth, [this](int i) { return ask_bits.next(i + 1); });
    }
};
