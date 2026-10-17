// Correctness test, run with ./bench --verify
//
// 1. Small hand written edge cases on each implementation.
// 2. The same random workload goes through all three implementations and
//    the results are compared after every single operation.
//    A is also compared against a slow but obviously correct book (NaiveBook)
//    for the first part of the workload.
#pragma once

#include <algorithm>
#include <iostream>
#include <map>
#include <string>

#include "book_a_map.hpp"
#include "book_b_vector.hpp"
#include "book_c_array.hpp"
#include "workload.hpp"

// Very slow reference: keeps only the orders and rebuilds the levels from
// scratch every time they are asked for. Too slow for 1M ops, fine for a few 10k.
class NaiveBook {
public:
    bool add_order(uint64_t id, Side side, int tick, long qty) {
        if (qty <= 0 || !valid_tick(side, tick)) return false;
        return orders_.emplace(id, Order{qty, tick, side}).second;
    }

    bool cancel_order(uint64_t id) { return orders_.erase(id) == 1; }

    // plain scan over every order, no levels involved
    BBO get_bbo() const {
        BBO b;
        for (const auto& kv : orders_) {
            const Order& o = kv.second;
            if (o.side == Side::Buy) {
                if (!b.has_bid || o.tick > b.bid_price) {
                    b.has_bid = true;
                    b.bid_price = o.tick;
                    b.bid_qty = 0;
                }
                if (o.tick == b.bid_price) b.bid_qty += o.qty;
            } else {
                if (!b.has_ask || o.tick < b.ask_price) {
                    b.has_ask = true;
                    b.ask_price = o.tick;
                    b.ask_qty = 0;
                }
                if (o.tick == b.ask_price) b.ask_qty += o.qty;
            }
        }
        return b;
    }

    std::vector<Level> get_bids(int depth = 10) const {
        std::map<int, long> m = sum_levels(Side::Buy);
        std::vector<Level> out;
        for (auto it = m.rbegin(); it != m.rend() && (int)out.size() < depth; ++it)
            out.push_back({it->first, it->second});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::map<int, long> m = sum_levels(Side::Sell);
        std::vector<Level> out;
        for (auto it = m.begin(); it != m.end() && (int)out.size() < depth; ++it)
            out.push_back({it->first, it->second});
        return out;
    }

private:
    std::map<int, long> sum_levels(Side side) const {
        std::map<int, long> m;
        for (const auto& kv : orders_)
            if (kv.second.side == side) m[kv.second.tick] += kv.second.qty;
        return m;
    }

    std::map<uint64_t, Order> orders_;
};

// ---------------------------------------------------------------- edge cases

template <class Book>
std::string run_edge_cases() {
    std::string why;
    auto check = [&](bool cond, const char* what) {
        if (!cond && why.empty()) why = what;
    };

    Book b;

    // empty book
    BBO e = b.get_bbo();
    check(!e.has_bid && !e.has_ask, "empty book should have no bid/ask");
    check(b.get_bids().empty() && b.get_asks().empty(), "empty book should have no levels");
    check(!b.cancel_order(1), "cancel on empty book should be false");

    // three bid levels, two orders share the best one
    check(b.add_order(1, Side::Buy, 9999, 10), "add 1");
    check(b.add_order(2, Side::Buy, 9999, 5), "add 2");
    check(b.add_order(3, Side::Buy, 9998, 7), "add 3");
    check(b.add_order(4, Side::Buy, 5000, 1), "add 4");
    // asks, again two orders share the best level
    check(b.add_order(5, Side::Sell, 10001, 4), "add 5");
    check(b.add_order(6, Side::Sell, 10001, 6), "add 6");
    check(b.add_order(7, Side::Sell, 15000, 2), "add 7");

    BBO x = b.get_bbo();
    check(x.has_bid && x.bid_price == 9999 && x.bid_qty == 15, "bbo bid after adds");
    check(x.has_ask && x.ask_price == 10001 && x.ask_qty == 10, "bbo ask after adds");

    // depth: only 3 bid levels exist but 10 are asked for
    auto bids = b.get_bids(10);
    check(bids.size() == 3, "get_bids(10) should return the 3 levels");
    check(bids.size() == 3 && bids[0] == Level{9999, 15} && bids[1] == Level{9998, 7} &&
              bids[2] == Level{5000, 1},
          "bid levels wrong");
    auto asks = b.get_asks(10);
    check(asks.size() == 2 && asks[0] == Level{10001, 10} && asks[1] == Level{15000, 2},
          "ask levels wrong");
    check(b.get_bids(2).size() == 2, "get_bids(2) should be cut to 2");
    check(b.get_bids(0).empty(), "get_bids(0) should be empty");

    // things that must be rejected and must not change the book
    check(!b.add_order(3, Side::Buy, 9000, 1), "duplicate id accepted");
    check(!b.add_order(8, Side::Buy, 10001, 1), "bid inside ask band accepted");
    check(!b.add_order(9, Side::Sell, 9999, 1), "ask inside bid band accepted");
    check(!b.add_order(10, Side::Buy, 4999, 1), "tick below 50.00 accepted");
    check(!b.add_order(11, Side::Sell, 15001, 1), "tick above 150.00 accepted");
    check(!b.add_order(12, Side::Buy, 9000, 0), "zero qty accepted");
    check(b.get_bids(10).size() == 3 && b.get_asks(10).size() == 2, "rejected add changed book");

    // cancels
    check(b.cancel_order(1), "cancel 1");
    check(!b.cancel_order(1), "second cancel of 1 should be false");
    check(!b.cancel_order(999), "cancel of unknown id should be false");
    x = b.get_bbo();
    check(x.bid_price == 9999 && x.bid_qty == 5, "qty not reduced after cancel");
    check(b.cancel_order(2), "cancel 2");
    x = b.get_bbo();
    check(x.has_bid && x.bid_price == 9998 && x.bid_qty == 7, "best bid did not move down");

    check(b.cancel_order(5) && b.cancel_order(6), "cancel 5, 6");
    x = b.get_bbo();
    check(x.has_ask && x.ask_price == 15000 && x.ask_qty == 2, "best ask did not move up");
    check(b.cancel_order(7), "cancel 7");
    check(!b.get_bbo().has_ask, "ask side should be empty");

    check(b.cancel_order(3) && b.cancel_order(4), "cancel 3, 4");
    e = b.get_bbo();
    check(!e.has_bid && !e.has_ask, "book should be empty again");
    check(b.get_bids().empty() && b.get_asks().empty(), "levels left after cancelling everything");
    return why;
}

// ------------------------------------------------------- random comparison

struct Verdict {
    long mismatches = 0;
    std::string first;

    void fail(long op_index, const std::string& what) {
        if (mismatches++ == 0) first = "op " + std::to_string(op_index) + ": " + what;
    }
};

// Compares one book against the book we trust (the "expected" one).
template <class Expected, class Actual>
void compare_after_op(Expected& exp, Actual& act, long i, Verdict& v, bool depth, bool full) {
    if (!(exp.get_bbo() == act.get_bbo())) v.fail(i, "BBO differs");
    if (depth) {
        if (!(exp.get_bids(kDepth) == act.get_bids(kDepth))) v.fail(i, "top 10 bids differ");
        if (!(exp.get_asks(kDepth) == act.get_asks(kDepth))) v.fail(i, "top 10 asks differ");
    }
    if (full) {
        // whole book, every level
        if (!(exp.get_bids(1 << 20) == act.get_bids(1 << 20))) v.fail(i, "full bid side differs");
        if (!(exp.get_asks(1 << 20) == act.get_asks(1 << 20))) v.fail(i, "full ask side differs");
    }
}

inline bool run_verify(int num_ops) {
    std::cout << "Running correctness test...\n\n";

    std::string edge_a = run_edge_cases<MapBook>();
    std::string edge_b = run_edge_cases<VectorBook>();
    std::string edge_c = run_edge_cases<ArrayBook>();

    std::cout << "Edge cases (empty book, unknown cancel, short depth, bad input):\n";
    std::cout << "  A: " << (edge_a.empty() ? "PASS" : "FAIL - " + edge_a) << "\n";
    std::cout << "  B: " << (edge_b.empty() ? "PASS" : "FAIL - " + edge_b) << "\n";
    std::cout << "  C: " << (edge_c.empty() ? "PASS" : "FAIL - " + edge_c) << "\n\n";

    const int naive_ops = std::min(num_ops, 50000);
    std::cout << "Random workload: " << num_ops << " ops, seed " << kSeed
              << ". Every op goes to A, B and C and the results are compared\n"
              << "(BBO after every op, top 10 levels every " << kDepthEvery
              << " ops, the whole book every 5000 ops).\n"
              << "A is also checked against a naive reference for the first " << naive_ops
              << " ops.\n\n";

    std::vector<Op> ops = make_workload(num_ops);

    MapBook a;
    VectorBook b;
    ArrayBook c;
    NaiveBook ref;
    Verdict va, vb, vc;
    long cancels = 0, double_cancels = 0;

    for (long i = 0; i < (long)ops.size(); i++) {
        const Op& op = ops[i];
        bool ra = false, rb = false, rc = false, rr = false;

        if (op.type == OpType::Add) {
            ra = a.add_order(op.id, op.side, op.tick, op.qty);
            rb = b.add_order(op.id, op.side, op.tick, op.qty);
            rc = c.add_order(op.id, op.side, op.tick, op.qty);
            if (i < naive_ops) rr = ref.add_order(op.id, op.side, op.tick, op.qty);
        } else if (op.type == OpType::Cancel) {
            ra = a.cancel_order(op.id);
            rb = b.cancel_order(op.id);
            rc = c.cancel_order(op.id);
            if (i < naive_ops) rr = ref.cancel_order(op.id);
            cancels++;

            // every 5th cancel, try to cancel the same id again. all must say false
            if (cancels % 5 == 0) {
                double_cancels++;
                bool x = a.cancel_order(op.id), y = b.cancel_order(op.id),
                     z = c.cancel_order(op.id);
                if (x) va.fail(i, "A cancelled an already cancelled order");
                if (y) vb.fail(i, "B cancelled an already cancelled order");
                if (z) vc.fail(i, "C cancelled an already cancelled order");
            }
        } else {
            rr = ra = rb = rc = true;  // BBO query, compared below
        }

        if (!(ra == rb)) vb.fail(i, "return value differs from A");
        if (!(ra == rc)) vc.fail(i, "return value differs from A");
        if (i < naive_ops && !(ra == rr)) va.fail(i, "return value differs from reference");

        bool depth = (i + 1) % kDepthEvery == 0;
        bool full = (i + 1) % 5000 == 0;
        compare_after_op(a, b, i, vb, depth, full);
        compare_after_op(a, c, i, vc, depth, full);
        if (i < naive_ops) compare_after_op(ref, a, i, va, depth, i % 1000 == 999);
    }

    std::cout << "Cancels of unknown / already cancelled ids tried: " << double_cancels << "\n";
    std::cout << "Live orders at the end: A=" << a.live_orders() << " B=" << b.live_orders()
              << " C=" << c.live_orders() << "\n\n";

    bool ok_a = edge_a.empty() && va.mismatches == 0;
    bool ok_b = edge_b.empty() && vb.mismatches == 0;
    bool ok_c = edge_c.empty() && vc.mismatches == 0;
    if (a.live_orders() != b.live_orders()) ok_b = false;
    if (a.live_orders() != c.live_orders()) ok_c = false;

    auto line = [](const char* name, bool ok, const Verdict& v) {
        std::cout << "Implementation " << name << ": " << (ok ? "PASS" : "FAIL");
        if (v.mismatches) std::cout << "  (" << v.mismatches << " mismatches, first at " << v.first << ")";
        std::cout << "\n";
    };
    line("A", ok_a, va);
    line("B", ok_b, vb);
    line("C", ok_c, vc);

    bool all = ok_a && ok_b && ok_c;
    std::cout << "\n"
              << (all ? "All implementations produce identical results."
                      : "Implementations do NOT agree.")
              << "\n";
    return all;
}
