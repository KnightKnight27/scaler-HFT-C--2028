// Correctness test (./bench --verify).
//
// 1. Edge-case unit checks run against each implementation.
// 2. Differential test: the benchmark workload and an adversarial stress workload
//    are replayed in lockstep through A, B, C and a deliberately simple reference
//    model. After every operation the return value, BBO, top-10 bids and top-10
//    asks of each implementation are compared with the reference; every 1,000
//    operations the full depth of both sides is compared as well.

#include <cstdio>
#include <map>
#include <memory>
#include <string>

#include "book_array.hpp"
#include "book_map.hpp"
#include "book_vector.hpp"
#include "driver.hpp"

namespace {

// Reference model: obviously-correct, slow. Orders in a std::map, quantities in a
// plain array, and every query is answered by scanning the array from the inside out.
class ReferenceBook {
public:
    bool add_order(uint64_t id, Side side, double price, long qty) {
        int32_t tick;
        if (!px::validate_order(side, price, qty, tick)) return false;
        if (orders_.count(id)) return false;
        orders_[id] = {tick, qty};
        qty_[tick - px::kMinTick] += qty;
        return true;
    }
    bool cancel_order(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return false;
        qty_[it->second.first - px::kMinTick] -= it->second.second;
        orders_.erase(it);
        return true;
    }
    BBO get_bbo() const {
        BBO b{};
        const auto bids = get_bids(1), asks = get_asks(1);
        if (!bids.empty()) b = {true, bids[0].price, bids[0].qty, false, 0, 0};
        if (!asks.empty()) { b.has_ask = true; b.ask_price = asks[0].price; b.ask_qty = asks[0].qty; }
        return b;
    }
    std::vector<Level> get_bids(int depth = 10) const {
        std::vector<Level> out;
        for (int32_t t = px::kBidMaxTick; t >= px::kBidMinTick && static_cast<int>(out.size()) < depth; --t)
            if (qty_[t - px::kMinTick]) out.push_back({px::to_price(t), qty_[t - px::kMinTick]});
        return out;
    }
    std::vector<Level> get_asks(int depth = 10) const {
        std::vector<Level> out;
        for (int32_t t = px::kAskMinTick; t <= px::kAskMaxTick && static_cast<int>(out.size()) < depth; ++t)
            if (qty_[t - px::kMinTick]) out.push_back({px::to_price(t), qty_[t - px::kMinTick]});
        return out;
    }
    size_t order_count() const { return orders_.size(); }

private:
    std::map<uint64_t, std::pair<int32_t, long>> orders_;  // id -> (tick, qty)
    std::vector<long> qty_ = std::vector<long>(px::kNumTicks, 0);
};

// ----------------------------- comparisons ---------------------------------

bool same(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
    return true;
}

bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}

std::string str(const BBO& b) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "{bid %s %.2f x %ld | ask %s %.2f x %ld}", b.has_bid ? "Y" : "N", b.bid_price,
                  b.bid_qty, b.has_ask ? "Y" : "N", b.ask_price, b.ask_qty);
    return buf;
}

std::string str(const std::vector<Level>& v) {
    std::string s = "[";
    char buf[64];
    for (size_t i = 0; i < v.size() && i < 12; ++i) {
        std::snprintf(buf, sizeof buf, "%s%.2fx%ld", i ? " " : "", v[i].price, v[i].qty);
        s += buf;
    }
    return s + (v.size() > 12 ? " ...]" : "]");
}

// Records the first few failures of one implementation.
struct Report {
    long checks = 0;
    long failures = 0;
    std::vector<std::string> messages;

    void check(bool ok, const std::string& what) {
        check_lazy(ok, [&] { return what; });
    }

    // Like check(), but only builds the message when the check fails.
    template <class MakeMessage>
    void check_lazy(bool ok, MakeMessage&& make) {
        ++checks;
        if (ok) return;
        ++failures;
        if (messages.size() < 5) messages.push_back(make());
    }
};

// ------------------------------ edge cases ---------------------------------

template <class Book>
void edge_cases(Report& r) {
    auto fresh = [] { return std::make_unique<Book>(); };
    {
        auto b = fresh();
        const BBO e = b->get_bbo();
        r.check(!e.has_bid && !e.has_ask, "empty book: BBO must have has_bid == has_ask == false");
        r.check(b->get_bids(10).empty() && b->get_asks(10).empty(), "empty book: get_bids/get_asks must be empty");
        r.check(!b->cancel_order(12345), "cancel of unknown id must return false");
    }
    {
        auto b = fresh();
        r.check(b->add_order(1, Side::Buy, 99.50, 10), "add valid bid");
        r.check(b->cancel_order(1), "cancel live order returns true");
        r.check(!b->cancel_order(1), "second cancel of same id returns false");
        r.check(!b->get_bbo().has_bid, "book empty again after cancel");
    }
    {
        auto b = fresh();
        b->add_order(1, Side::Buy, 99.00, 1);
        b->add_order(2, Side::Buy, 98.50, 2);
        b->add_order(3, Side::Buy, 98.00, 3);
        const auto bids = b->get_bids(10);
        r.check(same(bids, {{99.00, 1}, {98.50, 2}, {98.00, 3}}), "insufficient depth: 3 levels best-first, got " + str(bids));
        r.check(b->get_bids(2).size() == 2, "get_bids(2) returns 2 levels");
        r.check(b->get_bids(0).empty() && b->get_bids(-3).empty(), "non-positive depth returns no levels");
        r.check(b->get_asks(10).empty(), "no asks");
    }
    {
        auto b = fresh();
        b->add_order(10, Side::Sell, 101.00, 5);
        b->add_order(11, Side::Sell, 101.00, 7);
        b->add_order(12, Side::Sell, 101.00, 9);
        b->add_order(13, Side::Sell, 100.50, 4);
        b->add_order(14, Side::Sell, 120.00, 1);
        r.check(same(b->get_asks(10), {{100.50, 4}, {101.00, 21}, {120.00, 1}}),
                "asks ascending with quantities summed per level, got " + str(b->get_asks(10)));
        b->cancel_order(11);
        r.check(same(b->get_asks(2), {{100.50, 4}, {101.00, 14}}), "cancel reduces level qty");
        b->cancel_order(13);
        const BBO bbo = b->get_bbo();
        r.check(bbo.has_ask && bbo.ask_price == 101.00 && bbo.ask_qty == 14, "best ask moves after cancelling best: " + str(bbo));
        b->cancel_order(10);
        b->cancel_order(12);
        r.check(same(b->get_asks(10), {{120.00, 1}}), "emptied level disappears");
    }
    {
        auto b = fresh();
        b->add_order(20, Side::Buy, 99.99, 1);
        b->add_order(21, Side::Buy, 99.98, 2);
        b->add_order(22, Side::Sell, 100.01, 3);
        BBO bbo = b->get_bbo();
        r.check(bbo.has_bid && bbo.bid_price == 99.99 && bbo.bid_qty == 1 && bbo.ask_price == 100.01 && bbo.ask_qty == 3,
                "BBO with both sides: " + str(bbo));
        b->cancel_order(20);
        bbo = b->get_bbo();
        r.check(bbo.bid_price == 99.98 && bbo.bid_qty == 2, "best bid moves after cancelling best: " + str(bbo));
    }
    {
        auto b = fresh();
        r.check(!b->add_order(1, Side::Buy, 100.01, 1), "bid at ask price rejected");
        r.check(!b->add_order(2, Side::Sell, 99.99, 1), "ask at bid price rejected");
        r.check(!b->add_order(3, Side::Buy, 100.00, 1), "order at mid rejected");
        r.check(!b->add_order(4, Side::Buy, 49.99, 1), "bid below band rejected");
        r.check(!b->add_order(5, Side::Sell, 150.01, 1), "ask above band rejected");
        r.check(!b->add_order(6, Side::Buy, 99.995, 1), "off-tick price rejected");
        r.check(!b->add_order(7, Side::Buy, 99.00, 0), "zero qty rejected");
        r.check(!b->add_order(8, Side::Buy, 99.00, -1), "negative qty rejected");
        r.check(!b->get_bbo().has_bid && !b->get_bbo().has_ask, "rejected orders leave the book empty");
        r.check(b->add_order(9, Side::Buy, 50.00, 1) && b->add_order(10, Side::Buy, 99.99, 1) &&
                    b->add_order(11, Side::Sell, 100.01, 1) && b->add_order(12, Side::Sell, 150.00, 1),
                "band edge prices accepted");
        r.check(b->add_order(13, Side::Sell, 100.0 + 0.01, 1), "100.0 + 0.01 (inexact double) accepted as 100.01");
        r.check(!b->add_order(9, Side::Sell, 120.00, 5), "duplicate live id rejected");
        r.check(same(b->get_asks(10), {{100.01, 2}, {150.00, 1}}), "duplicate add left book unchanged");
        r.check(!b->cancel_order(4), "cancel of id that was rejected returns false");
    }
}

// ----------------------------- differential --------------------------------

// Everything an operation returns, so return values can be compared directly.
struct Outcome {
    bool ok = false;
    BBO bbo{};
    std::vector<Level> bids, asks;
};

template <class Book>
Outcome apply(Book& b, const Op& op) {
    Outcome o;
    switch (op.type) {
        case OpType::Add: o.ok = b.add_order(op.id, op.side, op.price, op.qty); break;
        case OpType::Cancel: o.ok = b.cancel_order(op.id); break;
        case OpType::Bbo: o.bbo = b.get_bbo(); break;
        case OpType::Depth:
            o.bids = b.get_bids(op.depth);
            o.asks = b.get_asks(op.depth);
            break;
    }
    return o;
}

bool same(const Outcome& a, const Outcome& b, OpType t) {
    switch (t) {
        case OpType::Add:
        case OpType::Cancel: return a.ok == b.ok;
        case OpType::Bbo: return same(a.bbo, b.bbo);
        case OpType::Depth: return same(a.bids, b.bids) && same(a.asks, b.asks);
    }
    return false;
}

const char* kTypeNames[] = {"add_order", "cancel_order", "get_bbo", "get_bids/get_asks"};

// Compares one implementation with the reference after operation `i`.
template <class Book>
void check_step(Report& r, Book& book, const ReferenceBook& ref, const Op& op, const Outcome& expect, size_t i,
                bool full_depth) {
    auto at = [&] { return "op " + std::to_string(i) + " (" + kTypeNames[static_cast<int>(op.type)] + "): "; };
    const Outcome got = apply(book, op);
    r.check_lazy(same(got, expect, op.type), [&] { return at() + "return value differs from reference"; });

    const BBO a = book.get_bbo(), e = ref.get_bbo();
    r.check_lazy(same(a, e), [&] { return at() + "BBO " + str(a) + " expected " + str(e); });

    const int depth = full_depth ? (1 << 30) : 10;
    const auto bids = book.get_bids(depth), ebids = ref.get_bids(depth);
    r.check_lazy(same(bids, ebids), [&] { return at() + "bids " + str(bids) + " expected " + str(ebids); });
    const auto asks = book.get_asks(depth), easks = ref.get_asks(depth);
    r.check_lazy(same(asks, easks), [&] { return at() + "asks " + str(asks) + " expected " + str(easks); });
    if (full_depth)
        r.check_lazy(book.order_count() == ref.order_count(), [&] { return at() + "live order count differs"; });
}

struct Reports {
    Report a, b, c;
};

void differential(const Workload& w, Reports& reps) {
    ReferenceBook ref;
    auto a = std::make_unique<OrderBookMap>();
    auto b = std::make_unique<OrderBookVector>();
    auto c = std::make_unique<OrderBookArray>();

    for (size_t i = 0; i < w.ops.size(); ++i) {
        const Op& op = w.ops[i];
        const Outcome expect = apply(ref, op);
        const bool full = (i % 1000 == 999) || i + 1 == w.ops.size();
        check_step(reps.a, *a, ref, op, expect, i, full);
        check_step(reps.b, *b, ref, op, expect, i, full);
        check_step(reps.c, *c, ref, op, expect, i, full);
    }
}

void print_failures(const char* label, const Report& r) {
    for (const std::string& m : r.messages) std::printf("    [%s] %s\n", label, m.c_str());
}

}  // namespace

int run_verify(size_t bench_ops, uint32_t seed) {
    std::printf("Running correctness test...\n\n");
    Reports total;

    // 1. Edge cases.
    Reports edge;
    edge_cases<OrderBookMap>(edge.a);
    edge_cases<OrderBookVector>(edge.b);
    edge_cases<OrderBookArray>(edge.c);
    std::printf("[1/3] Edge-case checks (%ld per implementation): A %s, B %s, C %s\n", edge.a.checks,
                edge.a.failures ? "FAIL" : "ok", edge.b.failures ? "FAIL" : "ok", edge.c.failures ? "FAIL" : "ok");
    print_failures("A", edge.a);
    print_failures("B", edge.b);
    print_failures("C", edge.c);

    // 2 + 3. Differential tests against the reference model.
    const Workload workloads[] = {make_benchmark_workload(bench_ops, seed), make_stress_workload(300000, seed + 1)};
    int step = 2;
    for (const Workload& w : workloads) {
        Reports r;
        differential(w, r);
        std::printf("[%d/3] %s workload: %zu ops (%zu add, %zu cancel, %zu bbo, %zu depth), %ld checks per implementation:"
                    " A %s, B %s, C %s\n",
                    step++, w.name.c_str(), w.ops.size(), w.count[0], w.count[1], w.count[2], w.count[3], r.a.checks,
                    r.a.failures ? "FAIL" : "ok", r.b.failures ? "FAIL" : "ok", r.c.failures ? "FAIL" : "ok");
        print_failures("A", r.a);
        print_failures("B", r.b);
        print_failures("C", r.c);
        total.a.failures += r.a.failures;
        total.b.failures += r.b.failures;
        total.c.failures += r.c.failures;
    }
    total.a.failures += edge.a.failures;
    total.b.failures += edge.b.failures;
    total.c.failures += edge.c.failures;

    std::printf("\nImplementation A: %s\n", total.a.failures ? "FAIL" : "PASS");
    std::printf("Implementation B: %s\n", total.b.failures ? "FAIL" : "PASS");
    std::printf("Implementation C: %s\n\n", total.c.failures ? "FAIL" : "PASS");

    const bool ok = !total.a.failures && !total.b.failures && !total.c.failures;
    std::printf(ok ? "All implementations produce identical results.\n"
                   : "MISMATCH: implementations do not agree (see failures above).\n");
    return ok ? 0 : 1;
}
