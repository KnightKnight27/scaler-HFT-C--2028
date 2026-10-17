#pragma once

// ./bench --verify
//
// 1. fixed edge cases on each book (the "required behavior" section and a few more)
// 2. C's hash table on its own against std::unordered_map
// 3. four random workloads, each played on a brute force reference book and on
//    A, B and C side by side. After every op every book is compared with the
//    reference: add/cancel return value, BBO, top 10 bids and asks, and the
//    bids and asks at a second depth that changes every op (-2 .. 22, and the
//    full book every 1000 ops)
// 4. the real 1M op benchmark workload, checked the same way as 3, and the
//    checksums from play() have to match as well

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "array_book.hpp"
#include "common.hpp"
#include "heap_book.hpp"
#include "map_book.hpp"
#include "workload.hpp"

// Dumb reference book: qty per tick in a plain vector and a full scan for
// every query. Slow, but simple enough that we trust it.
class NaiveBook {
public:
    NaiveBook() : qty_(MAX_ASK_TICK + 1, 0) {}

    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick;
        if (!valid_qty(qty) || !price_to_tick(side, price, tick)) return false;
        if (orders_.count(id)) return false;
        orders_[id] = {tick, qty};
        qty_[tick] += qty;
        return true;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        qty_[it->second.first] -= it->second.second;
        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b;
        auto bids = get_bids(1), asks = get_asks(1);
        if (!bids.empty()) {
            b.has_bid = true;
            b.bid_price = bids[0].price;
            b.bid_qty = bids[0].qty;
        }
        if (!asks.empty()) {
            b.has_ask = true;
            b.ask_price = asks[0].price;
            b.ask_qty = asks[0].qty;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int t = MAX_BID_TICK; t >= MIN_BID_TICK && (int)out.size() < depth; t--)
            if (qty_[t] > 0) out.push_back({to_price(t), qty_[t]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int t = MIN_ASK_TICK; t <= MAX_ASK_TICK && (int)out.size() < depth; t++)
            if (qty_[t] > 0) out.push_back({to_price(t), qty_[t]});
        return out;
    }

private:
    std::vector<long> qty_;
    std::unordered_map<uint64_t, std::pair<int, long>> orders_;
};

inline bool same_bbo(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (to_tick(a.bid_price) != to_tick(b.bid_price) || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (to_tick(a.ask_price) != to_tick(b.ask_price) || a.ask_qty != b.ask_qty)) return false;
    return true;
}

inline bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (to_tick(a[i].price) != to_tick(b[i].price) || a[i].qty != b[i].qty) return false;
    return true;
}

// ---------------------------------------------------------------- 1. edge cases

template <class Book>
bool edge_cases(std::string& why) {
#define CHECK(cond)                 \
    do {                            \
        if (!(cond)) {              \
            why = "failed: " #cond; \
            return false;           \
        }                           \
    } while (0)

    auto bk = std::make_unique<Book>();
    Book& book = *bk;
    BBO b;

    // empty book
    b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);
    CHECK(book.get_bids(10).empty() && book.get_asks(10).empty());

    // cancel something that never existed
    CHECK(!book.cancel_order(12345));

    // bad prices / qty get rejected
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(!book.add_order(1, Side::Buy, 100.00, 10));  // the mid is in neither band
    CHECK(!book.add_order(1, Side::Sell, 100.00, 10));
    CHECK(!book.add_order(1, Side::Buy, 49.99, 10));
    CHECK(!book.add_order(1, Side::Sell, 99.99, 10));  // ask below band
    CHECK(!book.add_order(1, Side::Sell, 150.01, 10));
    CHECK(!book.add_order(1, Side::Buy, 99.985, 10));  // not on the tick grid
    CHECK(!book.add_order(1, Side::Buy, 99.991, 10));
    CHECK(!book.add_order(1, Side::Buy, std::nan(""), 10));
    CHECK(!book.add_order(1, Side::Sell, inf, 10));
    CHECK(!book.add_order(1, Side::Buy, -inf, 10));
    CHECK(!book.add_order(1, Side::Sell, 1e300, 10));
    CHECK(!book.add_order(1, Side::Buy, -75.00, 10));
    CHECK(!book.add_order(1, Side::Buy, 99.00, 0));
    CHECK(!book.add_order(1, Side::Buy, 99.00, -5));
    b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);
    CHECK(!book.cancel_order(1));

    // ids at the ends of the range are normal ids
    CHECK(book.add_order(0, Side::Buy, 75.00, 1));
    CHECK(book.add_order(std::numeric_limits<uint64_t>::max(), Side::Sell, 125.00, 2));
    b = book.get_bbo();
    CHECK(b.has_bid && b.bid_qty == 1 && b.has_ask && b.ask_qty == 2);
    CHECK(book.cancel_order(0));
    CHECK(book.cancel_order(std::numeric_limits<uint64_t>::max()));
    CHECK(!book.cancel_order(0));
    b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);

    // the ticks right next to the mid are fine
    CHECK(book.add_order(5, Side::Buy, 99.99, 1) && book.add_order(6, Side::Sell, 100.01, 1));
    b = book.get_bbo();
    CHECK(to_tick(b.bid_price) == 9999 && to_tick(b.ask_price) == 10001);
    CHECK(book.cancel_order(5) && book.cancel_order(6));

    // biggest allowed qty, twice on one level (the level total needs 64 bits)
    if (sizeof(long) == 8) {
        long big = MAX_QTY;
        CHECK(!book.add_order(20, Side::Buy, 80.00, big + 1));
        CHECK(book.add_order(20, Side::Buy, 80.00, big));
        CHECK(book.add_order(21, Side::Buy, 80.00, big));
        auto lv = book.get_bids(5);
        CHECK(lv.size() == 1 && lv[0].qty == 2 * big);
        CHECK(book.cancel_order(20) && book.cancel_order(21));
        CHECK(book.get_bids(5).empty());
    }

    // three bid levels, two orders on one of them
    CHECK(book.add_order(1, Side::Buy, 99.50, 10));
    CHECK(book.add_order(2, Side::Buy, 99.98, 20));
    CHECK(book.add_order(3, Side::Buy, 99.50, 5));
    CHECK(book.add_order(4, Side::Buy, 50.00, 7));
    CHECK(!book.add_order(4, Side::Buy, 60.00, 7));  // duplicate id
    CHECK(!book.add_order(4, Side::Sell, 120.00, 7));

    auto bids = book.get_bids(10);  // only 3 levels exist
    CHECK(bids.size() == 3);
    CHECK(to_tick(bids[0].price) == 9998 && bids[0].qty == 20);
    CHECK(to_tick(bids[1].price) == 9950 && bids[1].qty == 15);
    CHECK(to_tick(bids[2].price) == 5000 && bids[2].qty == 7);
    CHECK(book.get_bids().size() == 3);  // default depth
    CHECK(book.get_bids(2).size() == 2);
    CHECK(book.get_bids(0).empty());
    CHECK(book.get_bids(-1).empty());
    CHECK(book.get_asks(10).empty());

    b = book.get_bbo();
    CHECK(b.has_bid && !b.has_ask && to_tick(b.bid_price) == 9998 && b.bid_qty == 20);

    // asks
    CHECK(book.add_order(10, Side::Sell, 100.01, 3));
    CHECK(book.add_order(11, Side::Sell, 150.00, 4));
    CHECK(book.add_order(12, Side::Sell, 100.01, 6));
    auto asks = book.get_asks(10);
    CHECK(asks.size() == 2);
    CHECK(to_tick(asks[0].price) == 10001 && asks[0].qty == 9);
    CHECK(to_tick(asks[1].price) == 15000 && asks[1].qty == 4);
    b = book.get_bbo();
    CHECK(b.has_ask && to_tick(b.ask_price) == 10001 && b.ask_qty == 9);

    // cancel the best bid -> next level becomes best
    CHECK(book.cancel_order(2));
    CHECK(!book.cancel_order(2));  // second cancel of same id
    b = book.get_bbo();
    CHECK(to_tick(b.bid_price) == 9950 && b.bid_qty == 15);

    // partial level cancel keeps the level
    CHECK(book.cancel_order(1));
    b = book.get_bbo();
    CHECK(to_tick(b.bid_price) == 9950 && b.bid_qty == 5);

    // cancel best ask orders one by one
    CHECK(book.cancel_order(10));
    CHECK(book.cancel_order(12));
    b = book.get_bbo();
    CHECK(b.has_ask && to_tick(b.ask_price) == 15000 && b.ask_qty == 4);

    // empty everything
    CHECK(book.cancel_order(3));
    CHECK(book.cancel_order(4));
    CHECK(book.cancel_order(11));
    b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);
    CHECK(book.get_bids(10).empty() && book.get_asks(10).empty());

    // default depth is 10
    for (int k = 0; k < 12; k++) {
        CHECK(book.add_order(200 + k, Side::Buy, to_price(9000 + k), 1));
        CHECK(book.add_order(300 + k, Side::Sell, to_price(11000 + k), 1));
    }
    CHECK(book.get_bids().size() == 10 && book.get_asks().size() == 10);
    for (int k = 0; k < 12; k++) CHECK(book.cancel_order(200 + k) && book.cancel_order(300 + k));

    // a level in the middle empties, then the best one (in B this leaves a
    // stale tick in the heap that has to be skipped)
    CHECK(book.add_order(30, Side::Buy, 99.00, 1));
    CHECK(book.add_order(31, Side::Buy, 98.00, 2));
    CHECK(book.add_order(32, Side::Buy, 97.00, 3));
    CHECK(book.cancel_order(31));
    bids = book.get_bids(10);
    CHECK(bids.size() == 2 && to_tick(bids[0].price) == 9900 && to_tick(bids[1].price) == 9700);
    CHECK(book.cancel_order(30));
    b = book.get_bbo();
    CHECK(to_tick(b.bid_price) == 9700 && b.bid_qty == 3);

    // a level comes back after being emptied
    CHECK(book.add_order(33, Side::Buy, 98.00, 4));
    b = book.get_bbo();
    CHECK(to_tick(b.bid_price) == 9800 && b.bid_qty == 4);
    CHECK(book.add_order(34, Side::Buy, 99.50, 5));
    CHECK(book.cancel_order(33));  // middle level empty again while 99.50 is best
    CHECK(book.add_order(35, Side::Buy, 98.00, 6));
    bids = book.get_bids(10);
    CHECK(bids.size() == 3 && to_tick(bids[1].price) == 9800 && bids[1].qty == 6);
    CHECK(book.cancel_order(34));
    b = book.get_bbo();
    CHECK(to_tick(b.bid_price) == 9800 && b.bid_qty == 6);

    // same on the ask side
    CHECK(book.add_order(40, Side::Sell, 101.00, 1));
    CHECK(book.add_order(41, Side::Sell, 102.00, 2));
    CHECK(book.add_order(42, Side::Sell, 103.00, 3));
    CHECK(book.cancel_order(41));
    CHECK(book.cancel_order(40));
    b = book.get_bbo();
    CHECK(to_tick(b.ask_price) == 10300 && b.ask_qty == 3);
    CHECK(book.add_order(43, Side::Sell, 102.00, 7));
    b = book.get_bbo();
    CHECK(to_tick(b.ask_price) == 10200 && b.ask_qty == 7);

    CHECK(book.cancel_order(32) && book.cancel_order(35) && book.cancel_order(42) && book.cancel_order(43));
    b = book.get_bbo();
    CHECK(!b.has_bid && !b.has_ask);

    // book still works after being emptied again
    CHECK(book.add_order(100, Side::Sell, 120.00, 1));
    b = book.get_bbo();
    CHECK(b.has_ask && to_tick(b.ask_price) == 12000 && b.ask_qty == 1);
    CHECK(book.order_count() == 1);
#undef CHECK
    return true;
}

// ---------------------------------------------------------------- 2. C's hash table

// OrderTable alone vs std::unordered_map. The key space is small so the same
// ids keep getting inserted and erased, and the table starts at 16 slots so it
// resizes a lot and probe clusters wrap around the end of the array. Every
// third seed uses ids that only differ in the high bits.
inline bool order_table_test(std::string& why) {
    for (uint32_t seed = 1; seed <= 9; seed++) {
        std::mt19937_64 rng(seed);
        OrderTable table(16);
        std::unordered_map<uint64_t, std::pair<int, int>> ref;
        uint64_t key_space = seed % 2 ? 3000 : 200000;
        int shift = seed % 3 == 0 ? 40 : 0;

        for (int i = 0; i < 200000; i++) {
            uint64_t id = (rng() % key_space) << shift;
            if (rng() % 2) {
                int tick = MIN_BID_TICK + (int)(rng() % 10001), qty = 1 + (int)(rng() % 100);
                bool fresh = ref.emplace(id, std::make_pair(tick, qty)).second;
                if (table.insert(id, tick, qty) != fresh) {
                    why = "OrderTable::insert returned the wrong value";
                    return false;
                }
            } else {
                auto it = ref.find(id);
                OrderTable::Slot* s = table.find(id);
                if ((it != ref.end()) != (s != nullptr)) {
                    why = "OrderTable::find found / didn't find an id";
                    return false;
                }
                if (s) {
                    if (s->tick != it->second.first || s->qty != it->second.second) {
                        why = "OrderTable::find returned the wrong slot";
                        return false;
                    }
                    table.erase(s);
                    ref.erase(it);
                }
            }
            if (table.size() != ref.size()) {
                why = "OrderTable::size is wrong";
                return false;
            }
        }
        for (const auto& kv : ref) {
            OrderTable::Slot* s = table.find(kv.first);
            if (!s || s->tick != kv.second.first || s->qty != kv.second.second) {
                why = "OrderTable lost an id";
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------- 3. random workloads

// What the reference book returned for one op.
struct RefState {
    bool result = false;  // add/cancel return value
    BBO bbo;
    std::vector<Level> bids, asks;  // depth 10
    int depth = 0;                  // second depth, changes every op
    std::vector<Level> bids_d, asks_d;
};

// One book being checked, and the first mismatch found in it.
template <class Book>
struct Checked {
    std::unique_ptr<Book> book = std::make_unique<Book>();
    std::string error;  // empty = no mismatch so far
};

template <class Book>
void check_op(Checked<Book>& c, const Op& op, size_t i, const RefState& ref) {
    if (!c.error.empty()) return;  // keep only the first error

    Book& book = *c.book;
    bool result = false;
    if (op.type == OpType::Add) result = book.add_order(op.id, op.side, op.price, op.qty);
    else if (op.type == OpType::Cancel) result = book.cancel_order(op.id);

    const char* what = nullptr;
    if (op.type != OpType::Bbo && result != ref.result)
        what = op.type == OpType::Add ? "add_order returned the wrong value" : "cancel_order returned the wrong value";
    else if (!same_bbo(book.get_bbo(), ref.bbo))
        what = "get_bbo is different";
    else if (!same_levels(book.get_bids(10), ref.bids))
        what = "get_bids(10) is different";
    else if (!same_levels(book.get_asks(10), ref.asks))
        what = "get_asks(10) is different";
    else if (!same_levels(book.get_bids(ref.depth), ref.bids_d))
        what = "get_bids(depth) is different";
    else if (!same_levels(book.get_asks(ref.depth), ref.asks_d))
        what = "get_asks(depth) is different";

    if (what) c.error = "op " + std::to_string(i) + ": " + what + " (depth " + std::to_string(ref.depth) + ")";
}

// Plays one workload on the reference book and on A, B and C at the same time.
inline void play_side_by_side(const std::vector<Op>& ops, Checked<MapBook>& a, Checked<HeapBook>& b,
                              Checked<ArrayBook>& c) {
    NaiveBook ref;
    RefState s;
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) s.result = ref.add_order(op.id, op.side, op.price, op.qty);
        else if (op.type == OpType::Cancel) s.result = ref.cancel_order(op.id);

        s.bbo = ref.get_bbo();
        s.bids = ref.get_bids(10);
        s.asks = ref.get_asks(10);
        s.depth = (i % 1000 == 999) ? 100000 : (int)(i % 25) - 2;
        s.bids_d = ref.get_bids(s.depth);
        s.asks_d = ref.get_asks(s.depth);

        check_op(a, op, i, s);
        check_op(b, op, i, s);
        check_op(c, op, i, s);
    }
}

// ---------------------------------------------------------------- run everything

inline int run_verify(size_t n_ops, uint32_t seed) {
    std::printf("Running correctness test...\n\n");

    std::string err[3];  // first error for A, B, C
    auto fail = [&](int k, const std::string& msg) {
        if (err[k].empty()) err[k] = msg;
    };

    std::string why;
    if (!edge_cases<MapBook>(why)) fail(0, "edge cases: " + why);
    if (!edge_cases<HeapBook>(why)) fail(1, "edge cases: " + why);
    if (!edge_cases<ArrayBook>(why)) fail(2, "edge cases: " + why);
    bool edge_ok = err[0].empty() && err[1].empty() && err[2].empty();
    std::printf("1. edge cases (empty book, bad prices/qty, unknown/double cancel, depth, emptied levels): %s\n",
                edge_ok ? "ok" : "FAILED");

    bool table_ok = order_table_test(why);
    if (!table_ok) fail(2, "hash table test: " + why);
    std::printf("2. C's OrderTable vs std::unordered_map, 9 x 200000 random inserts/erases: %s\n",
                table_ok ? "ok" : "FAILED");

    struct RandomWorkload {
        const char* name;
        int add_pct;
        bool random_ids;
        int band;
    };
    const RandomWorkload workloads[] = {
        {"normal 70/20/10 mix", 70, false, 5000},
        {"thin book, 45% add / 45% cancel", 45, false, 5000},
        {"random 64 bit ids", 70, true, 5000},
        {"random ids, thin book, only 20 ticks per side", 45, true, 20},
    };

    std::printf("3. random workloads, A, B, C and a brute force book side by side, compared after every op\n");
    std::printf("   (5%% of cancels go to unknown or already cancelled ids)\n");
    for (int k = 0; k < 4; k++) {
        const RandomWorkload& w = workloads[k];
        WorkloadOptions opt;
        opt.add_pct = w.add_pct;
        opt.random_ids = w.random_ids;
        opt.band = w.band;
        opt.bad_cancel_pct = 5;
        std::vector<Op> ops = make_workload(n_ops, seed + k, opt);
        WorkloadCounts c = count_ops(ops);
        std::printf("   %c) %s: %zu ops (add %zu, cancel %zu, bbo %zu), seed %u\n", 'a' + k, w.name, ops.size(),
                    c.add, c.cancel, c.bbo, seed + k);

        Checked<MapBook> A;
        Checked<HeapBook> B;
        Checked<ArrayBook> C;
        play_side_by_side(ops, A, B, C);
        std::string label = std::string(1, char('a' + k)) + ") ";
        if (!A.error.empty()) fail(0, label + A.error);
        if (!B.error.empty()) fail(1, label + B.error);
        if (!C.error.empty()) fail(2, label + C.error);
    }

    // the real benchmark workload: side by side like above, and all three must
    // end with the same checksum
    std::vector<Op> bench_ops = make_workload(1000000, 42);
    {
        Checked<MapBook> A;
        Checked<HeapBook> B;
        Checked<ArrayBook> C;
        play_side_by_side(bench_ops, A, B, C);
        if (!A.error.empty()) fail(0, "benchmark workload, " + A.error);
        if (!B.error.empty()) fail(1, "benchmark workload, " + B.error);
        if (!C.error.empty()) fail(2, "benchmark workload, " + C.error);
    }
    uint64_t sum[3];
    {
        auto book = std::make_unique<MapBook>();
        sum[0] = play(*book, bench_ops);
    }
    {
        auto book = std::make_unique<HeapBook>();
        sum[1] = play(*book, bench_ops);
    }
    {
        auto book = std::make_unique<ArrayBook>();
        sum[2] = play(*book, bench_ops);
    }
    bool sums_match = sum[0] == sum[1] && sum[1] == sum[2];
    std::printf("4. the benchmark workload itself (1M ops, seed 42), compared after every op like in 3\n");
    std::printf("   checksum: A=%llu B=%llu C=%llu %s\n\n", (unsigned long long)sum[0],
                (unsigned long long)sum[1], (unsigned long long)sum[2], sums_match ? "(match)" : "(MISMATCH)");

    bool all_ok = sums_match;
    const char* names = "ABC";
    for (int k = 0; k < 3; k++) {
        std::printf("Implementation %c: %s", names[k], err[k].empty() ? "PASS" : "FAIL");
        if (!err[k].empty()) std::printf("  (%s)", err[k].c_str());
        std::printf("\n");
        all_ok = all_ok && err[k].empty();
    }

    if (all_ok)
        std::printf("\nAll implementations produce identical results.\n");
    else
        std::printf("\nImplementations DISAGREE.\n");
    return all_ok ? 0 : 1;
}
