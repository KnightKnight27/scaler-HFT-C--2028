#pragma once

#include <cstdio>
#include <string>
#include <unordered_map>

#include "book_array.hpp"
#include "book_map.hpp"
#include "book_vector.hpp"
#include "workload.hpp"

// Naive on purpose: no cached best price, every query scans the price range.
class ReferenceBook {
public:
    ReferenceBook() : qty_(MAX_ASK_TICK + 1, 0) {}

    bool add_order(uint64_t id, Side side, double price, long qty) {
        int tick = checked_tick(side, price);
        if (tick < 0 || qty <= 0 || orders_.count(id)) return false;
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
        BBO b{false, 0.0, 0, false, 0.0, 0};
        auto bids = get_bids(1), asks = get_asks(1);
        if (!bids.empty()) b = {true, bids[0].price, bids[0].qty, false, 0.0, 0};
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

inline bool same(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
    return true;
}

inline bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}

struct Checker {
    std::string name;
    int failures = 0;

    void expect(bool ok, const std::string& what) {
        if (!ok && failures++ < 5) std::printf("  [%s] FAILED: %s\n", name.c_str(), what.c_str());
    }
};

template <class Book>
void edge_case_tests(Checker& c) {
    {
        Book b;
        BBO bbo = b.get_bbo();
        c.expect(!bbo.has_bid && !bbo.has_ask, "empty book has no bid and no ask");
        c.expect(b.get_bids(10).empty() && b.get_asks(10).empty(), "empty book has no depth");
        c.expect(!b.cancel_order(12345), "cancel unknown id on empty book returns false");
    }
    {
        Book b;
        c.expect(b.add_order(1, Side::Buy, 99.50, 10), "add bid");
        c.expect(b.add_order(2, Side::Buy, 99.50, 15), "second bid at same price");
        c.expect(b.add_order(3, Side::Buy, 99.40, 5), "bid at lower price");
        c.expect(b.add_order(4, Side::Buy, 98.00, 7), "third bid level");
        c.expect(b.add_order(5, Side::Sell, 100.25, 8), "add ask");

        BBO bbo = b.get_bbo();
        c.expect(bbo.has_bid && bbo.bid_price == 99.50 && bbo.bid_qty == 25, "quantities at a price are summed");
        c.expect(bbo.has_ask && bbo.ask_price == 100.25 && bbo.ask_qty == 8, "best ask");

        auto bids = b.get_bids(10);
        c.expect(bids.size() == 3, "get_bids(10) with 3 levels returns 3 levels");
        c.expect(bids.size() == 3 && bids[0].price == 99.50 && bids[1].price == 99.40 && bids[2].price == 98.00,
                 "bids are sorted best first");
        c.expect(b.get_bids(2).size() == 2, "depth limit is respected");

        c.expect(b.cancel_order(1), "cancel live order");
        c.expect(!b.cancel_order(1), "second cancel of same id returns false");
        c.expect(!b.cancel_order(999), "cancel unknown id returns false");
        c.expect(b.get_bbo().bid_qty == 15, "partial removal from a level");

        c.expect(b.cancel_order(2), "cancel last order at best bid");
        c.expect(b.get_bbo().bid_price == 99.40, "best bid moves to the next level");

        c.expect(b.cancel_order(5), "cancel only ask");
        c.expect(!b.get_bbo().has_ask && b.get_bbo().has_bid, "sides are independent");
    }
    {
        Book b;
        c.expect(!b.add_order(1, Side::Buy, 100.00, 10), "mid price is not a valid bid");
        c.expect(!b.add_order(2, Side::Sell, 100.00, 10), "mid price is not a valid ask");
        c.expect(!b.add_order(3, Side::Buy, 49.99, 10), "bid below 50.00 rejected");
        c.expect(!b.add_order(4, Side::Sell, 150.01, 10), "ask above 150.00 rejected");
        c.expect(!b.add_order(5, Side::Buy, 99.995, 10), "price off the 0.01 grid rejected");
        c.expect(!b.add_order(6, Side::Buy, 99.00, 0), "zero quantity rejected");
        c.expect(b.add_order(7, Side::Buy, 50.00, 10) && b.add_order(8, Side::Sell, 150.00, 10), "band edges accepted");
        c.expect(!b.add_order(7, Side::Sell, 120.00, 10), "duplicate live id rejected");
        BBO bbo = b.get_bbo();
        c.expect(bbo.bid_price == 50.00 && bbo.ask_price == 150.00, "band edge prices stored exactly");
    }
}

template <class Book>
void replay(const std::vector<Op>& ops, Checker& c) {
    ReferenceBook ref;
    Book book;
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        std::string at = "op " + std::to_string(i);
        if (op.type == OpType::Add)
            c.expect(book.add_order(op.id, op.side, op.price, op.qty) == ref.add_order(op.id, op.side, op.price, op.qty),
                     at + " add result");
        else if (op.type == OpType::Cancel)
            c.expect(book.cancel_order(op.id) == ref.cancel_order(op.id), at + " cancel result");

        c.expect(same(book.get_bbo(), ref.get_bbo()), at + " BBO");
        c.expect(same(book.get_bids(10), ref.get_bids(10)), at + " top 10 bids");
        c.expect(same(book.get_asks(10), ref.get_asks(10)), at + " top 10 asks");
        if (i % 10000 == 0 || i + 1 == ops.size()) {
            c.expect(same(book.get_bids(10000), ref.get_bids(10000)), at + " full bid depth");
            c.expect(same(book.get_asks(10000), ref.get_asks(10000)), at + " full ask depth");
        }
        if (op.type == OpType::Cancel) c.expect(!book.cancel_order(op.id), at + " repeated cancel returns false");
    }
}

template <class Book>
bool verify_one(const char* name, const std::vector<std::vector<Op>>& workloads) {
    Checker c{name};
    edge_case_tests<Book>(c);
    for (const auto& ops : workloads) replay<Book>(ops, c);
    std::printf("Implementation %s: %s\n", name, c.failures == 0 ? "PASS" : "FAIL");
    return c.failures == 0;
}

inline int run_verify() {
    std::printf("Running correctness test...\n");

    WorkloadConfig standard;

    WorkloadConfig churn;  // levels empty and the best price moves all the time
    churn.num_ops = 300000;
    churn.seed = 7;
    churn.add_pct = 50;
    churn.cancel_pct = 45;
    churn.band_ticks = 20;

    WorkloadConfig tiny;  // often empty on one or both sides
    tiny.num_ops = 100000;
    tiny.seed = 99;
    tiny.add_pct = 40;
    tiny.cancel_pct = 45;
    tiny.band_ticks = 3;

    std::vector<std::vector<Op>> workloads = {make_workload(standard), make_workload(churn), make_workload(tiny)};
    std::printf("Workloads: 1,000,000 ops (seed 42), 300,000 ops churn (seed 7), 100,000 ops tiny book (seed 99)\n");
    std::printf("Checked after every op: add/cancel result, BBO, top-10 bids and asks, repeated cancel.\n");
    std::printf("Full depth checked every 10,000 ops. Edge cases: empty book, unknown ids, short depth, bad prices.\n\n");

    bool ok = verify_one<MapBook>("A", workloads);
    ok = verify_one<VectorBook>("B", workloads) && ok;
    ok = verify_one<ArrayBook>("C", workloads) && ok;

    std::printf("\n%s\n", ok ? "All implementations produce identical results." : "Implementations DISAGREE.");
    return ok ? 0 : 1;
}
