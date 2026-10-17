// 3 order books, same api, different guts. integer ticks instead of doubles (see README)
// price 100.01 = tick 10001. bids 5000..9999, asks 10001..15000
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

using namespace std;



enum class Side { Buy, Sell };
struct Level { int tick; long qty; bool operator==(const Level& o) const { return tick == o.tick && qty == o.qty; } };
struct BBO {
    bool has_bid = false; int bid_tick = 0; long bid_qty = 0;
    bool has_ask = false; int ask_tick = 0; long ask_qty = 0;
    bool operator==(const BBO& o) const {
        return has_bid == o.has_bid && bid_tick == o.bid_tick && bid_qty == o.bid_qty &&
               has_ask == o.has_ask && ask_tick == o.ask_tick && ask_qty == o.ask_qty;
    }
};
struct Ord { Side side; int tick; long qty; };

constexpr int BID_LO = 5000, BID_HI = 9999, ASK_LO = 10001, ASK_HI = 15000, NL = 5000;
inline bool tick_ok(Side s, int t) { return s == Side::Buy ? (t >= BID_LO && t <= BID_HI) : (t >= ASK_LO && t <= ASK_HI); }

// A
// std::map per side for levels (best = begin), unordered_map for id -> order
class BookA {
    map<int, long, greater<int>> bids;
    map<int, long> asks;
    unordered_map<uint64_t, Ord> orders;

    template <class M> static void sub(M& m, const Ord& o) {
        auto it = m.find(o.tick);
        if ((it->second -= o.qty) == 0) m.erase(it);
    }
    template <class M> static vector<Level> top(const M& m, int d) {
        vector<Level> v;
        for (auto& [t, q] : m) { if ((int)v.size() >= d) break; v.push_back({t, q}); }
        return v;
    }
public:
    void add_order(uint64_t id, Side s, int tick, long qty) {
        if (!tick_ok(s, tick) || qty <= 0) return;
        if (!orders.emplace(id, Ord{s, tick, qty}).second) return;   // dup id
        if (s == Side::Buy) bids[tick] += qty; else asks[tick] += qty;
    }
    bool cancel_order(uint64_t id) {
        auto it = orders.find(id);
        if (it == orders.end()) return false;
        Ord o = it->second;
        orders.erase(it);
        if (o.side == Side::Buy) sub(bids, o); else sub(asks, o);
        return true;
    }
    BBO get_bbo() const {
        BBO b;
        if (!bids.empty()) { b.has_bid = true; b.bid_tick = bids.begin()->first; b.bid_qty = bids.begin()->second; }
        if (!asks.empty()) { b.has_ask = true; b.ask_tick = asks.begin()->first; b.ask_qty = asks.begin()->second; }
        return b;
    }
    vector<Level> get_bids(int d = 10) const { return top(bids, d); }
    vector<Level> get_asks(int d = 10) const { return top(asks, d); }
};

// B
// levels in a sorted vector per side, best is at the BACK (bids ascending, asks descending)
// so best lookup is just back(). orders live in a std::map (red black tree) keyed by id
class BookB {
    vector<Level> bids, asks;
    map<uint64_t, Ord> orders;

    static vector<Level>::iterator find(vector<Level>& v, bool buy, int tick) {
        return lower_bound(v.begin(), v.end(), tick, [buy](const Level& l, int t) { return buy ? l.tick < t : l.tick > t; });
    }
    static vector<Level> top(const vector<Level>& v, int d) {
        vector<Level> r;
        for (int i = (int)v.size() - 1; i >= 0 && (int)r.size() < d; i--) r.push_back(v[i]);
        return r;
    }
    static void bestof(const vector<Level>& v, bool& has, int& tick, long& qty) {
        if (v.empty()) return;
        has = true; tick = v.back().tick; qty = v.back().qty;
    }
public:
    BookB() { bids.reserve(NL); asks.reserve(NL); }
    void add_order(uint64_t id, Side s, int tick, long qty) {
        if (!tick_ok(s, tick) || qty <= 0) return;
        if (!orders.emplace(id, Ord{s, tick, qty}).second) return;
        bool buy = s == Side::Buy;
        auto& v = buy ? bids : asks;
        auto it = find(v, buy, tick);
        if (it != v.end() && it->tick == tick) it->qty += qty;
        else v.insert(it, Level{tick, qty});
    }
    bool cancel_order(uint64_t id) {
        auto oi = orders.find(id);
        if (oi == orders.end()) return false;
        Ord o = oi->second;
        orders.erase(oi);
        bool buy = o.side == Side::Buy;
        auto& v = buy ? bids : asks;
        auto it = find(v, buy, o.tick);
        if ((it->qty -= o.qty) == 0) v.erase(it);
        return true;
    }
    BBO get_bbo() const {
        BBO b;
        bestof(bids, b.has_bid, b.bid_tick, b.bid_qty);
        bestof(asks, b.has_ask, b.ask_tick, b.ask_qty);
        return b;
    }
    vector<Level> get_bids(int d = 10) const { return top(bids, d); }
    vector<Level> get_asks(int d = 10) const { return top(asks, d); }
};

// C (mine)
// qty per tick in plain arrays (index = tick - lo), cached best bid/ask index.
// id -> order in my own flat open addressing hash (linear probing, tombstones),
// preallocated up front so the hot path doesnt malloc. if best level empties we scan
// for the next non empty one (max 5000 steps, usually 1-2)
class BookC {
    struct Slot { uint64_t id; Ord o; uint8_t st; };   // st: 0 empty, 1 used, 2 tomb
    long bq[NL] = {}, aq[NL] = {};
    int best_bid = -1, best_ask = -1;                  // index into arrays, -1 = empty side
    vector<Slot> t;
    size_t mask = 0, live = 0, dirty = 0;              // dirty = used + tombs
    int shift = 0;

    void init(size_t cap) { t.assign(cap, Slot{}); mask = cap - 1; shift = 64 - __builtin_ctzll(cap); live = dirty = 0; }
    size_t h(uint64_t id) const { return (id * 0x9E3779B97F4A7C15ull) >> shift; }
    Slot* find(uint64_t id) {
        size_t i = h(id);
        while (t[i].st) { if (t[i].st == 1 && t[i].id == id) return &t[i]; i = (i + 1) & mask; }
        return nullptr;
    }
    void rehash() {
        vector<Slot> old = move(t);
        init(live * 2 > old.size() ? old.size() * 2 : old.size());
        for (auto& s : old) {
            if (s.st != 1) continue;
            size_t i = h(s.id);
            while (t[i].st) i = (i + 1) & mask;
            t[i] = s; live++; dirty++;
        }
    }
    bool insert(uint64_t id, const Ord& o) {
        if ((dirty + 1) * 10 > t.size() * 7) rehash();
        size_t i = h(id);
        Slot* tomb = nullptr;
        while (t[i].st) {
            if (t[i].st == 1 && t[i].id == id) return false;
            if (t[i].st == 2 && !tomb) tomb = &t[i];
            i = (i + 1) & mask;
        }
        Slot* s = tomb ? tomb : &t[i];
        if (!tomb) dirty++;
        s->id = id; s->o = o; s->st = 1; live++;
        return true;
    }
public:
    BookC() { init(1 << 20); }
    void add_order(uint64_t id, Side s, int tick, long qty) {
        if (!tick_ok(s, tick) || qty <= 0) return;
        if (!insert(id, Ord{s, tick, qty})) return;
        if (s == Side::Buy) { int i = tick - BID_LO; bq[i] += qty; if (i > best_bid) best_bid = i; }
        else { int i = tick - ASK_LO; aq[i] += qty; if (best_ask < 0 || i < best_ask) best_ask = i; }
    }
    bool cancel_order(uint64_t id) {
        Slot* sl = find(id);
        if (!sl) return false;
        Ord o = sl->o;
        sl->st = 2; live--;
        if (o.side == Side::Buy) {
            int i = o.tick - BID_LO;
            if ((bq[i] -= o.qty) == 0 && i == best_bid) while (best_bid >= 0 && !bq[best_bid]) best_bid--;
        } else {
            int i = o.tick - ASK_LO;
            if ((aq[i] -= o.qty) == 0 && i == best_ask) {
                while (best_ask < NL && !aq[best_ask]) best_ask++;
                if (best_ask == NL) best_ask = -1;
            }
        }
        return true;
    }
    BBO get_bbo() const {
        BBO b;
        if (best_bid >= 0) { b.has_bid = true; b.bid_tick = best_bid + BID_LO; b.bid_qty = bq[best_bid]; }
        if (best_ask >= 0) { b.has_ask = true; b.ask_tick = best_ask + ASK_LO; b.ask_qty = aq[best_ask]; }
        return b;
    }
    vector<Level> get_bids(int d = 10) const {
        vector<Level> v;
        for (int i = best_bid; i >= 0 && (int)v.size() < d; i--) if (bq[i]) v.push_back({i + BID_LO, bq[i]});
        return v;
    }
    vector<Level> get_asks(int d = 10) const {
        vector<Level> v;
        if (best_ask < 0) return v;
        for (int i = best_ask; i < NL && (int)v.size() < d; i++) if (aq[i]) v.push_back({i + ASK_LO, aq[i]});
        return v;
    }
};
