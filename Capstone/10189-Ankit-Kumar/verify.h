// verify.h - correctness test (./bench --verify).
//
// 1. Edge cases from the assignment (empty book, unknown / double cancel, insufficient
//    depth, ...) run against each implementation.
// 2. Random workloads are replayed through each implementation and through a brute-force
//    ReferenceBook. After EVERY operation the return value, get_bbo(), get_bids(10) and
//    get_asks(10) must match; every 1000 operations the full depth must match too.
// An implementation PASSes only if every check agrees, so if all three pass they all
// produced identical results on the same workload.
#pragma once

#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "array_book.h"
#include "common.h"
#include "map_book.h"
#include "vector_book.h"
#include "workload.h"

// Deliberately the simplest book possible: qty per tick in a plain array, and every query
// scans the price band from the top. Slow, but easy to see that it is right.
class ReferenceBook {
public:
    bool add_order(OrderId id, Side side, Price price, Qty qty) {
        if (!valid_price(side, price) || !valid_qty(qty)) return false;
        if (!orders_.emplace(id, Rec{price, qty}).second) return false;
        qty_at_[price] += qty;
        return true;
    }

    bool cancel_order(OrderId id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        qty_at_[it->second.price] -= it->second.qty;
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
        for (Price p = kBidMax; p >= kBidMin && static_cast<int>(out.size()) < depth; --p)
            if (qty_at_[p] != 0) out.push_back(Level{p, qty_at_[p]});
        return out;
    }

    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (Price p = kAskMin; p <= kAskMax && static_cast<int>(out.size()) < depth; ++p)
            if (qty_at_[p] != 0) out.push_back(Level{p, qty_at_[p]});
        return out;
    }

private:
    struct Rec {
        Price price;
        Qty qty;
    };
    std::vector<Qty> qty_at_ = std::vector<Qty>(kAskMax + 1, 0);  // indexed by tick
    std::unordered_map<OrderId, Rec> orders_;
};

namespace verify_detail {

inline std::string to_string(const std::vector<Level>& levels) {
    std::ostringstream os;
    os << '[';
    for (size_t i = 0; i < levels.size(); ++i) {
        if (i) os << ", ";
        os << format_price(levels[i].price) << " x " << levels[i].qty;
    }
    os << ']';
    return os.str();
}

inline std::string to_string(const BBO& b) {
    std::ostringstream os;
    os << "bid " << (b.has_bid ? format_price(b.bid_price) + " x " + std::to_string(b.bid_qty) : "none")
       << ", ask " << (b.has_ask ? format_price(b.ask_price) + " x " + std::to_string(b.ask_qty) : "none");
    return os.str();
}

// Collects failed checks for one implementation; prints the first few.
struct Report {
    const char* impl;
    int checks = 0;
    int failures = 0;

    void check(bool ok, const std::string& what) {
        ++checks;
        if (!ok) fail(what);
    }

    // Same, but the message is only built if the check fails (the random workloads do
    // millions of checks, and formatting a message for each one would dominate the run).
    template <class MakeMessage>
    void check_lazy(bool ok, MakeMessage make_message) {
        ++checks;
        if (!ok) fail(make_message());
    }

    void fail(const std::string& what) {
        if (++failures <= 5) std::printf("    [%s] FAILED: %s\n", impl, what.c_str());
    }
};

// ---------- 1. edge cases ----------
template <class Book>
void edge_cases(Report& r) {
    const Price b9999 = to_ticks(99.99), b9998 = to_ticks(99.98), b9000 = to_ticks(90.00);
    const Price a10001 = to_ticks(100.01), a10002 = to_ticks(100.02);
    {
        Book book;
        BBO e = book.get_bbo();
        r.check(!e.has_bid && !e.has_ask, "empty book: get_bbo() must have has_bid == has_ask == false");
        r.check(book.get_bids(10).empty() && book.get_asks(10).empty(), "empty book: depth must be empty");
        r.check(!book.cancel_order(42), "cancel of an id that never existed must return false");
    }
    {
        Book book;
        r.check(book.add_order(1, Side::Buy, b9998, 10), "add bid 99.98 x 10");
        r.check(book.add_order(2, Side::Buy, b9998, 5), "add second bid at 99.98");
        r.check(book.add_order(3, Side::Buy, b9999, 7), "add bid 99.99 x 7");
        r.check(book.add_order(4, Side::Buy, b9000, 1), "add bid 90.00 x 1");
        r.check(book.add_order(5, Side::Sell, a10002, 4), "add ask 100.02 x 4");
        r.check(book.add_order(6, Side::Sell, a10001, 3), "add ask 100.01 x 3");

        BBO b = book.get_bbo();
        r.check(b == BBO{true, b9999, 7, true, a10001, 3}, "BBO should be 99.99 x 7 / 100.01 x 3, got " + to_string(b));

        auto bids = book.get_bids(10);
        r.check(bids == std::vector<Level>{{b9999, 7}, {b9998, 15}, {b9000, 1}},
                "get_bids(10) with only 3 levels must return those 3, best first, qty summed; got " + to_string(bids));
        auto asks = book.get_asks(10);
        r.check(asks == std::vector<Level>{{a10001, 3}, {a10002, 4}}, "get_asks(10) got " + to_string(asks));
        r.check(book.get_bids(1) == std::vector<Level>{{b9999, 7}}, "get_bids(1) returns only the best level");
        r.check(book.get_bids(0).empty() && book.get_asks(-3).empty(), "depth <= 0 returns nothing");

        r.check(!book.add_order(3, Side::Sell, a10001, 50), "re-using a live id must be rejected");
        r.check(book.get_asks(1) == std::vector<Level>{{a10001, 3}}, "a rejected add must not change the book");

        r.check(book.cancel_order(3), "cancel the only order at the best bid");
        b = book.get_bbo();
        r.check(b.has_bid && b.bid_price == b9998 && b.bid_qty == 15, "best bid must fall to 99.98 x 15, got " + to_string(b));
        r.check(!book.cancel_order(3), "cancelling the same id twice must return false");

        r.check(book.cancel_order(1), "cancel one of two orders at 99.98");
        r.check(book.get_bids(10) == std::vector<Level>{{b9998, 5}, {b9000, 1}}, "partial level cancel");

        for (OrderId id : {2, 4, 5, 6}) r.check(book.cancel_order(id), "cancel order " + std::to_string(id));
        b = book.get_bbo();
        r.check(!b.has_bid && !b.has_ask, "book must be empty again after cancelling everything");
    }
    {
        Book book;
        // band edges are valid, one tick outside is not
        r.check(book.add_order(10, Side::Buy, kBidMin, 1), "bid at 50.00 is valid");
        r.check(book.add_order(11, Side::Buy, kBidMax, 1), "bid at 99.99 is valid");
        r.check(book.add_order(12, Side::Sell, kAskMin, 1), "ask at 100.01 is valid");
        r.check(book.add_order(13, Side::Sell, kAskMax, 1), "ask at 150.00 is valid");
        r.check(!book.add_order(20, Side::Buy, kBidMin - 1, 1), "bid at 49.99 is rejected");
        r.check(!book.add_order(21, Side::Buy, to_ticks(100.00), 1), "bid at 100.00 (the mid) is rejected");
        r.check(!book.add_order(22, Side::Sell, to_ticks(100.00), 1), "ask at 100.00 (the mid) is rejected");
        r.check(!book.add_order(23, Side::Sell, kAskMax + 1, 1), "ask at 150.01 is rejected");
        r.check(!book.add_order(24, Side::Sell, kAskMin, 0), "qty 0 is rejected");
        r.check(!book.add_order(25, Side::Sell, kAskMin, -5), "negative qty is rejected");
        r.check(!book.cancel_order(20), "a rejected order cannot be cancelled");
        r.check(book.get_bids(10) == std::vector<Level>{{kBidMax, 1}, {kBidMin, 1}}, "bids at both band edges");
        r.check(book.get_asks(10) == std::vector<Level>{{kAskMin, 1}, {kAskMax, 1}}, "asks at both band edges");

        // extreme ids are ordinary ids
        r.check(book.add_order(0, Side::Buy, b9000, 2), "id 0 is a valid id");
        r.check(book.add_order(std::numeric_limits<OrderId>::max(), Side::Buy, b9000, 3), "id 2^64-1 is a valid id");
        auto with_extremes = book.get_bids(10);
        r.check(with_extremes.size() == 3 && with_extremes[1] == Level{b9000, 5}, "both extreme-id orders are at 90.00");
        r.check(book.cancel_order(0) && book.cancel_order(std::numeric_limits<OrderId>::max()), "cancel extreme ids");

        // walking down 5000 bid levels one by one exercises every bitmap word
        Book deep;
        OrderId id = 100;
        for (Price p = kBidMin; p <= kBidMax; ++p) deep.add_order(id++, Side::Buy, p, 1);
        r.check(deep.get_bids(kLevelsPerSide + 10).size() == static_cast<size_t>(kLevelsPerSide),
                "all 5000 bid levels present");
        bool ok = true;
        for (OrderId i = 100 + kLevelsPerSide - 1; i >= 100 && ok; --i) {  // cancel best-first
            ok = deep.cancel_order(i);
            BBO bb = deep.get_bbo();
            const Price expect = kBidMin + static_cast<Price>(i - 100) - 1;
            ok = ok && (i == 100 ? !bb.has_bid : (bb.has_bid && bb.bid_price == expect));
        }
        r.check(ok, "best bid must step down one tick at a time while cancelling from the top");
    }
}

// ---------- 2. random workloads vs. the reference book ----------
template <class Book>
void compare_on_workload(Report& r, const Workload& w, const char* label) {
    Book book(64);  // start tiny so the order tables have to grow (rehash) many times
    ReferenceBook ref;
    int before = r.failures;

    auto at = [&](size_t i, const std::string& what) { return std::string(label) + " op " + std::to_string(i) + ": " + what; };

    for (size_t i = 0; i < w.ops.size() && r.failures - before < 5; ++i) {
        const Op& op = w.ops[i];
        if (op.type == OpType::Add) {
            bool got = book.add_order(op.id, op.side, op.price, op.qty);
            bool exp = ref.add_order(op.id, op.side, op.price, op.qty);
            r.check_lazy(got == exp, [&] { return at(i, "add_order returned " + std::to_string(got)); });
        } else if (op.type == OpType::Cancel) {
            bool got = book.cancel_order(op.id);
            bool exp = ref.cancel_order(op.id);
            r.check_lazy(got == exp, [&] { return at(i, "cancel_order returned " + std::to_string(got)); });
            r.check_lazy(!book.cancel_order(op.id), [&] { return at(i, "second cancel of the same id must be false"); });
        }

        BBO gb = book.get_bbo(), eb = ref.get_bbo();
        r.check_lazy(gb == eb, [&] { return at(i, "BBO " + to_string(gb) + ", expected " + to_string(eb)); });
        auto gbid = book.get_bids(10), ebid = ref.get_bids(10);
        r.check_lazy(gbid == ebid, [&] { return at(i, "get_bids(10) " + to_string(gbid) + ", expected " + to_string(ebid)); });
        auto gask = book.get_asks(10), eask = ref.get_asks(10);
        r.check_lazy(gask == eask, [&] { return at(i, "get_asks(10) " + to_string(gask) + ", expected " + to_string(eask)); });

        if (i % 1000 == 999 || i + 1 == w.ops.size()) {
            r.check_lazy(book.get_bids(kLevelsPerSide) == ref.get_bids(kLevelsPerSide), [&] { return at(i, "full bid depth differs"); });
            r.check_lazy(book.get_asks(kLevelsPerSide) == ref.get_asks(kLevelsPerSide), [&] { return at(i, "full ask depth differs"); });
        }
    }
}

struct NamedWorkload {
    const char* label;
    const char* description;
    WorkloadConfig cfg;
};

inline std::vector<NamedWorkload> verify_workloads() {
    const int full = kLevelsPerSide;
    return {
        {"benchmark", "the benchmark workload: 70% add / 20% cancel / 10% BBO, full price band",
         WorkloadConfig{42, 1'000'000, 70, 20, full, IdPattern::Sequential}},
        {"churn", "45% add / 45% cancel on the 8 ticks nearest the mid (levels empty and refill, best moves a lot)",
         WorkloadConfig{7, 300'000, 45, 45, 8, IdPattern::Sequential}},
        {"sparse", "50% add / 48% cancel, full band (small book with big gaps between levels)",
         WorkloadConfig{2026, 300'000, 50, 48, full, IdPattern::Sequential}},
        {"scrambled", "60% add / 35% cancel with random-looking 64-bit ids (hash collisions)",
         WorkloadConfig{11, 300'000, 60, 35, full, IdPattern::Scrambled}},
        {"strided", "60% add / 35% cancel with ids k << 20 (ids that share their low bits)",
         WorkloadConfig{13, 200'000, 60, 35, full, IdPattern::Strided}},
    };
}

template <class Book>
Report verify_impl(const char* name, const std::vector<std::pair<NamedWorkload, Workload>>& workloads) {
    Report r{name};
    edge_cases<Book>(r);
    for (const auto& [nw, w] : workloads) compare_on_workload<Book>(r, w, nw.label);
    return r;
}

}  // namespace verify_detail

// Returns 0 if all implementations pass.
inline int run_verify() {
    using namespace verify_detail;
    std::printf("Running correctness test...\n\n");
    std::printf("Every implementation is checked against a brute-force reference book:\n");
    std::printf("  - edge cases: empty book, unknown and double cancel, insufficient depth,\n");
    std::printf("    price band edges, duplicate ids, qty <= 0, a 5000-level walk down the book\n");

    std::vector<std::pair<NamedWorkload, Workload>> workloads;
    for (const NamedWorkload& nw : verify_workloads()) {
        Workload w = make_workload(nw.cfg);
        std::printf("  - %-9s %7zu ops, seed %-4u: %s\n", nw.label, w.ops.size(), nw.cfg.seed, nw.description);
        workloads.emplace_back(nw, std::move(w));
    }
    std::printf("  books start with room for 64 orders, so every hash table has to grow many times\n");
    std::printf("  after every op: return value, get_bbo(), get_bids(10), get_asks(10);\n");
    std::printf("  every 1000 ops: full bid and ask depth\n\n");

    Report reports[] = {
        verify_impl<MapOrderBook>("A", workloads),
        verify_impl<VectorOrderBook>("B", workloads),
        verify_impl<ArrayOrderBook>("C", workloads),
    };

    std::printf("\n");
    bool all = true;
    for (const Report& r : reports) {
        std::printf("Implementation %s: %s  (%d checks, %d failed)\n", r.impl, r.failures == 0 ? "PASS" : "FAIL",
                    r.checks, r.failures);
        all = all && r.failures == 0;
    }
    std::printf("\n%s\n", all ? "All implementations produce identical results."
                              : "Implementations DISAGREE - see failures above.");
    return all ? 0 : 1;
}
