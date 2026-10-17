// test.cpp
// Correctness tests for all three order books.
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

#include "common.h"
#include "implem_a.h"
#include "implem_b.h"
#include "implem_c.h"
#include "workload.h"

// ---------------------------------------------------------------
// Reference book: deliberately simple, so I can trust it.
// Plain std::map for orders and for levels, nothing clever.
// ---------------------------------------------------------------
class ReferenceBook {
public:
    void add_order(uint64_t id, Side side, int tick, long qty) {
        orders_[id] = Order{side, tick, qty};
        levels(side)[tick] += qty;
    }

    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;

        std::map<int, long>& m = levels(it->second.side);
        m[it->second.tick] -= it->second.qty;
        if (m[it->second.tick] == 0) m.erase(it->second.tick);

        orders_.erase(it);
        return true;
    }

    BBO get_bbo() const {
        BBO b{};
        if (!bids_.empty()) {
            b.has_bid  = true;
            b.bid_tick = bids_.rbegin()->first;   // highest bid
            b.bid_qty  = bids_.rbegin()->second;
        }
        if (!asks_.empty()) {
            b.has_ask  = true;
            b.ask_tick = asks_.begin()->first;    // lowest ask
            b.ask_qty  = asks_.begin()->second;
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = bids_.rbegin(); it != bids_.rend() && (int)out.size() < depth; ++it)
            out.push_back(Level{it->first, it->second});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (auto it = asks_.begin(); it != asks_.end() && (int)out.size() < depth; ++it)
            out.push_back(Level{it->first, it->second});
        return out;
    }

private:
    struct Order {
        Side side;
        int  tick;
        long qty;
    };

    std::map<int, long>& levels(Side s) { return s == Side::Buy ? bids_ : asks_; }

    std::map<int, long> bids_;
    std::map<int, long> asks_;
    std::map<uint64_t, Order> orders_;
};

// ---------------------------------------------------------------
// Part 1: small edge-case tests (run once per implementation)
// ---------------------------------------------------------------
template <typename Book>
int run_small_tests() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::cout << "    FAILED: " << what << "\n";
            ++fails;
        }
    };

    // 1. empty book
    {
        Book b;
        BBO x = b.get_bbo();
        check(!x.has_bid && !x.has_ask, "empty book: has_bid and has_ask should be false");
        check(b.get_bids(10).empty(), "empty book: get_bids should be empty");
        check(b.get_asks(10).empty(), "empty book: get_asks should be empty");
    }

    // 2. unknown order cancel
    {
        Book b;
        check(!b.cancel_order(1),   "cancel on empty book should return false");
        b.add_order(1, Side::Buy, 9000, 10);
        check(!b.cancel_order(999), "cancel of unknown id should return false");
        check(!b.cancel_order(0),   "cancel of id 0 should return false");
    }

    // 3. double cancel
    {
        Book b;
        b.add_order(1, Side::Buy, 9000, 10);
        check(b.cancel_order(1),  "first cancel should return true");
        check(!b.cancel_order(1), "second cancel should return false");
        check(!b.get_bbo().has_bid, "book should be empty after the only order is cancelled");
    }

    // 4. quantities add up at the same price
    {
        Book b;
        b.add_order(1, Side::Buy, 9998, 10);
        b.add_order(2, Side::Buy, 9998, 5);
        BBO x = b.get_bbo();
        check(x.bid_tick == 9998 && x.bid_qty == 15, "two orders at one price should sum to 15");
        b.cancel_order(1);
        x = b.get_bbo();
        check(x.bid_tick == 9998 && x.bid_qty == 5, "after cancelling one, qty should be 5");
    }

    // 5. best price moves when the best level is cancelled
    {
        Book b;
        b.add_order(1, Side::Buy, 9990, 7);
        b.add_order(2, Side::Buy, 9998, 3);
        b.add_order(3, Side::Sell, 10050, 4);
        b.add_order(4, Side::Sell, 10001, 6);
        b.cancel_order(2);
        b.cancel_order(4);
        BBO x = b.get_bbo();
        check(x.has_bid && x.bid_tick == 9990 && x.bid_qty == 7, "best bid should fall back to 9990");
        check(x.has_ask && x.ask_tick == 10050 && x.ask_qty == 4, "best ask should move up to 10050");
    }

    // 6. asking for more depth than there are levels
    {
        Book b;
        b.add_order(1, Side::Buy, 9998, 1);
        b.add_order(2, Side::Buy, 9997, 2);
        b.add_order(3, Side::Buy, 9996, 3);
        std::vector<Level> v = b.get_bids(10);
        check(v.size() == 3, "get_bids(10) with 3 levels should return 3");
        check(v.size() == 3 && v[0].tick == 9998 && v[2].tick == 9996,
              "bids should be ordered best (highest) first");
        check(b.get_asks(10).empty(), "no asks were added, so get_asks should be empty");
    }

    // 7. depth limit and ask ordering
    {
        Book b;
        for (int i = 0; i < 15; ++i)
            b.add_order(i + 1, Side::Sell, 10001 + i, 1);
        std::vector<Level> v = b.get_asks(10);
        check(v.size() == 10, "get_asks(10) with 15 levels should return 10");
        check(v.size() == 10 && v[0].tick == 10001 && v[9].tick == 10010,
              "asks should be ordered best (lowest) first");
    }

    // 8. price edges of each side
    {
        Book b;
        b.add_order(1, Side::Buy, 5000, 1);     // lowest bid
        b.add_order(2, Side::Buy, 9999, 2);     // highest bid
        b.add_order(3, Side::Sell, 10001, 3);   // lowest ask
        b.add_order(4, Side::Sell, 15000, 4);   // highest ask
        BBO x = b.get_bbo();
        check(x.bid_tick == 9999 && x.ask_tick == 10001, "BBO at the edges of the price range");
        b.cancel_order(2);
        b.cancel_order(3);
        x = b.get_bbo();
        check(x.has_bid && x.bid_tick == 5000, "best bid should become the lowest tick 5000");
        check(x.has_ask && x.ask_tick == 15000, "best ask should become the highest tick 15000");
        b.cancel_order(1);
        b.cancel_order(4);
        x = b.get_bbo();
        check(!x.has_bid && !x.has_ask, "book should be empty after cancelling the edge orders");
    }

    return fails;
}

template <typename Book>
bool small_tests(const char* name) {
    std::cout << "  " << name << " small tests:\n";
    int fails = run_small_tests<Book>();
    if (fails == 0) std::cout << "    all passed\n";
    return fails == 0;
}

// ---------------------------------------------------------------
// Part 2: workloads compared against the reference
// ---------------------------------------------------------------
struct Checker {
    const char* name;
    long mismatches = 0;
    long first_bad_op = -1;

    void record(bool ok, long op_index) {
        if (!ok) {
            if (mismatches == 0) first_bad_op = op_index;
            ++mismatches;
        }
    }
};

// A cancel-heavy workload on a very narrow price band.
// With only `band` price levels per side, the book empties out often, which the
// full-band workload almost never does. It also cancels some orders that were
// already cancelled, to check that a double cancel returns false.
static std::vector<Op> generate_narrow_workload(int num_ops, int bid_lo, int ask_lo,
                                                int band, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> percent(0, 99);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<int> offset(0, band - 1);

    std::vector<Op> ops;
    ops.reserve(num_ops);

    std::vector<uint64_t> live_ids;
    std::vector<uint64_t> dead_ids;
    uint64_t next_id = 1;

    for (int i = 0; i < num_ops; ++i) {
        int roll = percent(rng);
        OpType type;
        if (roll < 45)      type = OpType::Add;
        else if (roll < 90) type = OpType::Cancel;
        else                type = OpType::BBO;

        // nothing to cancel: query instead, so the book stays near empty
        if (type == OpType::Cancel && live_ids.empty()) type = OpType::BBO;

        Op op{};
        op.type = type;

        if (type == OpType::Add) {
            op.id   = next_id++;
            op.side = (percent(rng) < 50) ? Side::Buy : Side::Sell;
            op.tick = (op.side == Side::Buy) ? bid_lo + offset(rng) : ask_lo + offset(rng);
            op.qty  = qty_dist(rng);
            live_ids.push_back(op.id);
        } else if (type == OpType::Cancel) {
            if (!dead_ids.empty() && percent(rng) < 10) {
                // cancel an order that is already gone (must return false)
                std::uniform_int_distribution<size_t> pick(0, dead_ids.size() - 1);
                op.id = dead_ids[pick(rng)];
            } else {
                std::uniform_int_distribution<size_t> pick(0, live_ids.size() - 1);
                size_t k = pick(rng);
                op.id = live_ids[k];
                dead_ids.push_back(op.id);
                live_ids[k] = live_ids.back();
                live_ids.pop_back();
            }
        }
        ops.push_back(op);
    }
    return ops;
}

// Runs one workload through the reference and A, B, C.
// The BBO AND the top 10 levels of both sides are compared after EVERY operation.
static void compare_workload(const char* title, const std::vector<Op>& ops,
                             Checker& ca, Checker& cb, Checker& cc) {
    long a0 = ca.mismatches, b0 = cb.mismatches, c0 = cc.mismatches;

    ReferenceBook ref;
    OrderBookA a;
    OrderBookB b;
    OrderBookC c;

    for (size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];

        if (op.type == OpType::Add) {
            ref.add_order(op.id, op.side, op.tick, op.qty);
            a.add_order(op.id, op.side, op.tick, op.qty);
            b.add_order(op.id, op.side, op.tick, op.qty);
            c.add_order(op.id, op.side, op.tick, op.qty);
        } else if (op.type == OpType::Cancel) {
            bool r = ref.cancel_order(op.id);
            ca.record(a.cancel_order(op.id) == r, i);
            cb.record(b.cancel_order(op.id) == r, i);
            cc.record(c.cancel_order(op.id) == r, i);
        }

        BBO rb = ref.get_bbo();
        ca.record(a.get_bbo() == rb, i);
        cb.record(b.get_bbo() == rb, i);
        cc.record(c.get_bbo() == rb, i);

        std::vector<Level> rbids = ref.get_bids(10);
        std::vector<Level> rasks = ref.get_asks(10);
        ca.record(a.get_bids(10) == rbids && a.get_asks(10) == rasks, i);
        cb.record(b.get_bids(10) == rbids && b.get_asks(10) == rasks, i);
        cc.record(c.get_bids(10) == rbids && c.get_asks(10) == rasks, i);
    }

    std::cout << "  " << title << "\n"
              << "    mismatches: A=" << ca.mismatches - a0
              << "  B=" << cb.mismatches - b0
              << "  C=" << cc.mismatches - c0 << "\n";
}

int main() {
    // ---------- small cases ----------
    std::cout << "Running small-case tests...\n";
    bool a_small = small_tests<OrderBookA>("A");
    bool b_small = small_tests<OrderBookB>("B");
    bool c_small = small_tests<OrderBookC>("C");

    // ---------- workloads ----------
    std::cout << "\nRunning workload comparisons (BBO and top-10 depth checked after every op)...\n";
    Checker ca{"A"}, cb{"B"}, cc{"C"};

    compare_workload("1,000,000 ops, full price band (the benchmark workload)",
                     generate_workload(), ca, cb, cc);
    compare_workload("200,000 ops, cancel-heavy, 10-tick band near the mid price",
                     generate_narrow_workload(200000, 9990, 10001, 10, 7), ca, cb, cc);
    compare_workload("200,000 ops, cancel-heavy, 10-tick band at the range edges (5000 / 15000)",
                     generate_narrow_workload(200000, BID_MIN_TICK, ASK_MAX_TICK - 9, 10, 8),
                     ca, cb, cc);

    // ---------- report ----------
    std::cout << "\nResults:\n";
    bool all_ok = true;
    Checker* all[3] = {&ca, &cb, &cc};
    bool small_ok[3] = {a_small, b_small, c_small};
    for (int k = 0; k < 3; ++k) {
        bool ok = (all[k]->mismatches == 0) && small_ok[k];
        std::cout << "Implementation " << all[k]->name << ": " << (ok ? "PASS" : "FAIL");
        if (all[k]->mismatches > 0)
            std::cout << "  (" << all[k]->mismatches << " mismatches, first at op "
                      << all[k]->first_bad_op << ")";
        std::cout << "\n";
        if (!ok) all_ok = false;
    }

    if (all_ok)
        std::cout << "All implementations produce identical results.\n";
    else
        std::cout << "Some implementations FAILED.\n";

    return all_ok ? 0 : 1;
}
