// benchmark + correctness test for the 3 order books
// build: g++ -std=c++17 -O2 -o bench bench.cpp
// usage: ./bench --impl A|B|C [--runs 5] [--ops 1000000]
//        ./bench --verify

#include <iostream>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <memory>

#include "common.h"
#include "book_a.h"
#include "book_b.h"
#include "book_c.h"

using namespace std;
using Clock = chrono::steady_clock;

enum OpType { ADD, CANCEL, QUERY };

struct Op {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

// generated up front so rng isn't timed
vector<Op> make_workload(int n, unsigned seed) {
    mt19937 rng(seed);
    uniform_int_distribution<int> pct(0, 99);
    uniform_int_distribution<int> side_d(0, 1);
    uniform_int_distribution<int> qty_d(1, 100);
    uniform_int_distribution<int> bid_tick(MIN_TICK, MAX_BID_TICK);
    uniform_int_distribution<int> ask_tick(MIN_ASK_TICK, MAX_TICK);

    vector<Op> ops;
    ops.reserve(n);
    vector<uint64_t> live;  // ids currently in the book
    uint64_t next_id = 1;

    for (int i = 0; i < n; i++) {
        int r = pct(rng);
        Op op{};
        if (r < 70 || (r < 90 && live.empty())) {
            op.type = ADD;
            op.id = next_id++;
            op.side = side_d(rng) == 0 ? Side::Buy : Side::Sell;
            int t = (op.side == Side::Buy) ? bid_tick(rng) : ask_tick(rng);
            op.price = tick_to_price(t);
            op.qty = qty_d(rng);
            live.push_back(op.id);
        } else if (r < 90) {
            op.type = CANCEL;
            uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            op.id = live[k];
            live[k] = live.back();
            live.pop_back();
        } else {
            op.type = QUERY;
        }
        ops.push_back(op);
    }
    return ops;
}

struct Result {
    double total_ms;
    double ns_add, ns_cancel, ns_bbo, ns_depth;
    long long checksum;
    BBO final_bbo;
};

template <class Book>
Result run_once(const vector<Op>& ops) {
    unique_ptr<Book> book(new Book());

    long long t_add = 0, t_cancel = 0, t_bbo = 0, t_depth = 0;
    long long n_add = 0, n_cancel = 0, n_bbo = 0, n_depth = 0;
    long long checksum = 0;

    auto start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == ADD) {
            auto a = Clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            auto b = Clock::now();
            t_add += chrono::duration_cast<chrono::nanoseconds>(b - a).count();
            n_add++;
        } else if (op.type == CANCEL) {
            auto a = Clock::now();
            bool ok = book->cancel_order(op.id);
            auto b = Clock::now();
            t_cancel += chrono::duration_cast<chrono::nanoseconds>(b - a).count();
            n_cancel++;
            checksum += ok;
        } else {
            auto a = Clock::now();
            BBO bbo = book->get_bbo();
            auto b = Clock::now();
            t_bbo += chrono::duration_cast<chrono::nanoseconds>(b - a).count();
            n_bbo++;
            checksum += bbo.bid_qty + bbo.ask_qty;
        }

        if (i % 100 == 99) {
            auto a = Clock::now();
            vector<Level> bl = book->get_bids(10);
            vector<Level> al = book->get_asks(10);
            auto b = Clock::now();
            t_depth += chrono::duration_cast<chrono::nanoseconds>(b - a).count();
            n_depth++;
            for (auto& l : bl) checksum += l.qty;
            for (auto& l : al) checksum += l.qty;
        }
    }
    auto end = Clock::now();

    Result r;
    r.total_ms = chrono::duration<double, milli>(end - start).count();
    r.ns_add = n_add ? (double)t_add / n_add : 0;
    r.ns_cancel = n_cancel ? (double)t_cancel / n_cancel : 0;
    r.ns_bbo = n_bbo ? (double)t_bbo / n_bbo : 0;
    r.ns_depth = n_depth ? (double)t_depth / n_depth : 0;
    r.checksum = checksum;
    r.final_bbo = book->get_bbo();
    return r;
}

double median(vector<double> v) {
    sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class Book>
void run_bench(const char* name, const vector<Op>& ops, int runs) {
    vector<double> tot, add, can, bbo, dep;
    Result last{};
    for (int i = 0; i < runs; i++) {
        last = run_once<Book>(ops);
        tot.push_back(last.total_ms);
        add.push_back(last.ns_add);
        can.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        dep.push_back(last.ns_depth);
        printf("  run %d: total %.2f ms\n", i + 1, last.total_ms);
    }

    printf("\nImplementation %s (median of %d runs, %zu ops)\n", name, runs, ops.size());
    printf("Total time        : %.2f ms\n", median(tot));
    printf("ns / add_order    : %.1f\n", median(add));
    printf("ns / cancel_order : %.1f\n", median(can));
    printf("ns / get_bbo      : %.1f\n", median(bbo));
    printf("ns / depth query  : %.1f  (get_bids(10) + get_asks(10))\n", median(dep));

    BBO b = last.final_bbo;
    printf("\nFinal BBO:\n");
    if (b.has_bid) printf("Bid: %.2f x %ld\n", b.bid_price, b.bid_qty);
    else printf("Bid: none\n");
    if (b.has_ask) printf("Ask: %.2f x %ld\n", b.ask_price, b.ask_qty);
    else printf("Ask: none\n");
    printf("\nChecksum: %lld\n", last.checksum);
}

// ---------------- correctness ----------------

bool same_bbo(const BBO& x, const BBO& y) {
    if (x.has_bid != y.has_bid || x.has_ask != y.has_ask) return false;
    if (x.has_bid && (x.bid_price != y.bid_price || x.bid_qty != y.bid_qty)) return false;
    if (x.has_ask && (x.ask_price != y.ask_price || x.ask_qty != y.ask_qty)) return false;
    return true;
}

bool same_levels(const vector<Level>& x, const vector<Level>& y) {
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); i++)
        if (x[i].price != y[i].price || x[i].qty != y[i].qty) return false;
    return true;
}

// cases from the spec
template <class Book>
bool edge_cases(const char* name) {
    bool ok = true;
    unique_ptr<Book> b(new Book());

    BBO e = b->get_bbo();
    if (e.has_bid || e.has_ask) { printf("  [%s] empty book bbo wrong\n", name); ok = false; }
    if (b->cancel_order(12345)) { printf("  [%s] cancel unknown returned true\n", name); ok = false; }

    b->add_order(1, Side::Buy, 99.50, 10);
    b->add_order(2, Side::Buy, 99.50, 5);
    b->add_order(3, Side::Buy, 98.00, 7);
    b->add_order(4, Side::Buy, 97.25, 1);
    b->add_order(5, Side::Sell, 100.01, 20);

    vector<Level> bl = b->get_bids(10);
    if (bl.size() != 3 || bl[0].price != 99.50 || bl[0].qty != 15 || bl[2].price != 97.25) {
        printf("  [%s] insufficient depth / aggregation wrong\n", name); ok = false;
    }
    if (b->get_asks(10).size() != 1) { printf("  [%s] ask depth wrong\n", name); ok = false; }

    if (!b->cancel_order(1)) { printf("  [%s] cancel live returned false\n", name); ok = false; }
    if (b->cancel_order(1)) { printf("  [%s] double cancel returned true\n", name); ok = false; }
    BBO x = b->get_bbo();
    if (!x.has_bid || x.bid_price != 99.50 || x.bid_qty != 5) { printf("  [%s] bbo after cancel wrong\n", name); ok = false; }

    b->cancel_order(2);
    x = b->get_bbo();
    if (x.bid_price != 98.00) { printf("  [%s] best bid not moved after level emptied\n", name); ok = false; }

    b->cancel_order(5);
    x = b->get_bbo();
    if (x.has_ask) { printf("  [%s] ask side should be empty\n", name); ok = false; }
    return ok;
}

int run_verify(const vector<Op>& ops) {
    printf("Running correctness test...\n\n");

    bool okA = edge_cases<OrderBookA>("A");
    bool okB = edge_cases<OrderBookB>("B");
    bool okC = edge_cases<OrderBookC>("C");

    // run all three together, A is the reference
    unique_ptr<OrderBookA> a(new OrderBookA());
    unique_ptr<OrderBookB> b(new OrderBookB());
    unique_ptr<OrderBookC> c(new OrderBookC());
    long long badB = 0, badC = 0;

    for (size_t i = 0; i < ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == ADD) {
            a->add_order(op.id, op.side, op.price, op.qty);
            b->add_order(op.id, op.side, op.price, op.qty);
            c->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == CANCEL) {
            bool ra = a->cancel_order(op.id);
            bool rb = b->cancel_order(op.id);
            bool rc = c->cancel_order(op.id);
            if (rb != ra) badB++;
            if (rc != ra) badC++;
        }

        BBO ba = a->get_bbo();
        vector<Level> bidsA = a->get_bids(10), asksA = a->get_asks(10);
        if (!same_bbo(ba, b->get_bbo()) || !same_levels(bidsA, b->get_bids(10)) ||
            !same_levels(asksA, b->get_asks(10))) {
            if (badB == 0) printf("  B differs from A first at op %zu\n", i);
            badB++;
        }
        if (!same_bbo(ba, c->get_bbo()) || !same_levels(bidsA, c->get_bids(10)) ||
            !same_levels(asksA, c->get_asks(10))) {
            if (badC == 0) printf("  C differs from A first at op %zu\n", i);
            badC++;
        }
    }

    if (!same_levels(a->get_bids(1000), b->get_bids(1000)) || !same_levels(a->get_asks(1000), b->get_asks(1000))) badB++;
    if (!same_levels(a->get_bids(1000), c->get_bids(1000)) || !same_levels(a->get_asks(1000), c->get_asks(1000))) badC++;

    for (uint64_t id : {1ULL, 2ULL, 999999999ULL}) {
        bool ra = a->cancel_order(id);
        if (b->cancel_order(id) != ra) badB++;
        if (c->cancel_order(id) != ra) badC++;
    }

    okB = okB && badB == 0;
    okC = okC && badC == 0;

    printf("Checked %zu operations (BBO + 10 levels each side after every op)\n\n", ops.size());
    printf("Implementation A: %s\n", okA ? "PASS" : "FAIL");
    printf("Implementation B: %s\n", okB ? "PASS" : "FAIL");
    printf("Implementation C: %s\n", okC ? "PASS" : "FAIL");

    if (okA && okB && okC) {
        printf("\nAll implementations produce identical results.\n");
        return 0;
    }
    printf("\nMISMATCH found (B bad ops: %lld, C bad ops: %lld)\n", badB, badC);
    return 1;
}

int main(int argc, char** argv) {
    string impl = "";
    bool verify = false;
    int runs = 5;
    int nops = 1000000;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--impl") && i + 1 < argc) impl = argv[++i];
        else if (!strcmp(argv[i], "--verify")) verify = true;
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ops") && i + 1 < argc) nops = atoi(argv[++i]);
        else {
            printf("usage: %s --impl A|B|C [--runs N] [--ops N]  |  --verify\n", argv[0]);
            return 1;
        }
    }

    vector<Op> ops = make_workload(nops, 42);

    if (verify)
        return run_verify(ops);

    if (impl == "A") run_bench<OrderBookA>("A", ops, runs);
    else if (impl == "B") run_bench<OrderBookB>("B", ops, runs);
    else if (impl == "C") run_bench<OrderBookC>("C", ops, runs);
    else {
        printf("pick --impl A, B or C (or --verify)\n");
        return 1;
    }
    return 0;
}
