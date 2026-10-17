// Order book capstone - Vansh Dobhal (10099)
//
// ./bench --impl A|B|C [--runs N]    benchmark one implementation (default 5 runs, prints median)
// ./bench --verify                    check that all three implementations give the same results
// ./bench --impl none                 only builds the workload (used as a perf stat baseline)

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <memory>

#include "book_map.hpp"
#include "book_vector.hpp"
#include "book_array.hpp"

using namespace std;
using Clock = chrono::steady_clock;

const int NUM_OPS = 1000000;
const int DEPTH_EVERY = 100;

enum OpType { ADD, CANCEL, QUERY_BBO };

struct Op {
    OpType type;
    Side side;
    uint64_t id;
    double price;
    long qty;
};

// builds the same workload every time (seed 42)
// 70% add, 20% cancel, 10% bbo. cancels always pick an order that is still live
vector<Op> make_workload() {
    mt19937 rng(42);
    vector<Op> ops;
    ops.reserve(NUM_OPS);

    vector<uint64_t> live;
    uint64_t next_id = 1;

    for (int i = 0; i < NUM_OPS; i++) {
        int r = rng() % 100;
        Op op{};

        if (r < 70 || (r < 90 && live.empty())) {
            op.type = ADD;
            op.id = next_id++;
            op.side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            int tick;
            if (op.side == Side::Buy)
                tick = MIN_BID_TICK + rng() % (MAX_BID_TICK - MIN_BID_TICK + 1);
            else
                tick = MIN_ASK_TICK + rng() % (MAX_ASK_TICK - MIN_ASK_TICK + 1);
            op.price = to_price(tick);
            op.qty = 1 + rng() % 100;
            live.push_back(op.id);
        } else if (r < 90) {
            op.type = CANCEL;
            size_t k = rng() % live.size();
            op.id = live[k];
            live[k] = live.back();
            live.pop_back();
        } else {
            op.type = QUERY_BBO;
        }
        ops.push_back(op);
    }
    return ops;
}

struct Result {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    long long checksum;
    BBO final_bbo;
};

inline long long ns_between(Clock::time_point a, Clock::time_point b) {
    return chrono::duration_cast<chrono::nanoseconds>(b - a).count();
}

template <typename Book>
Result run_once(const vector<Op> &ops) {
    unique_ptr<Book> book(new Book());
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    long long adds = 0, cancels = 0, bbos = 0;
    long long checksum = 0;

    auto start = Clock::now();
    for (int i = 0; i < (int)ops.size(); i++) {
        const Op &op = ops[i];

        if (op.type == ADD) {
            auto t1 = Clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            auto t2 = Clock::now();
            add_ns += ns_between(t1, t2);
            adds++;
        } else if (op.type == CANCEL) {
            auto t1 = Clock::now();
            bool ok = book->cancel_order(op.id);
            auto t2 = Clock::now();
            cancel_ns += ns_between(t1, t2);
            cancels++;
            checksum += ok;
        } else {
            auto t1 = Clock::now();
            BBO b = book->get_bbo();
            auto t2 = Clock::now();
            bbo_ns += ns_between(t1, t2);
            bbos++;
            checksum += b.bid_qty + b.ask_qty;
        }

        if ((i + 1) % DEPTH_EVERY == 0) {
            vector<Level> bids = book->get_bids(10);
            vector<Level> asks = book->get_asks(10);
            for (auto &l : bids) checksum += l.qty;
            for (auto &l : asks) checksum += l.qty;
        }
    }
    auto end = Clock::now();

    Result res;
    res.total_ms = ns_between(start, end) / 1e6;
    res.ns_add = (double)add_ns / adds;
    res.ns_cancel = (double)cancel_ns / cancels;
    res.ns_bbo = (double)bbo_ns / bbos;
    res.checksum = checksum;
    res.final_bbo = book->get_bbo();
    return res;
}

double median(vector<double> v) {
    sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <typename Book>
void bench(const char *name, int runs) {
    vector<Op> ops = make_workload();
    vector<double> total, add, cancel, bbo;
    Result last{};

    for (int r = 0; r < runs; r++) {
        last = run_once<Book>(ops);
        total.push_back(last.total_ms);
        add.push_back(last.ns_add);
        cancel.push_back(last.ns_cancel);
        bbo.push_back(last.ns_bbo);
        printf("run %d: total %.2f ms | add %.1f ns | cancel %.1f ns | bbo %.1f ns\n",
               r + 1, last.total_ms, last.ns_add, last.ns_cancel, last.ns_bbo);
    }

    printf("\nImplementation %s, median of %d runs (%d ops)\n", name, runs, NUM_OPS);
    printf("Total time    : %.2f ms\n", median(total));
    printf("ns / add      : %.1f\n", median(add));
    printf("ns / cancel   : %.1f\n", median(cancel));
    printf("ns / bbo      : %.1f\n", median(bbo));

    printf("\nFinal BBO:\n");
    printf("Bid: %.2f x %ld\n", last.final_bbo.bid_price, last.final_bbo.bid_qty);
    printf("Ask: %.2f x %ld\n", last.final_bbo.ask_price, last.final_bbo.ask_qty);
    printf("\nChecksum: %lld\n", last.checksum);
}

// ---------------- correctness ----------------

bool same_bbo(const BBO &a, const BBO &b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask)
        return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty))
        return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty))
        return false;
    return true;
}

bool same_levels(const vector<Level> &a, const vector<Level> &b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty)
            return false;
    return true;
}

// small hand written cases that each book has to pass on its own
template <typename Book>
bool edge_tests() {
    unique_ptr<Book> b(new Book());

    // empty book
    BBO e = b->get_bbo();
    if (e.has_bid || e.has_ask) return false;
    if (!b->get_bids(10).empty() || !b->get_asks(10).empty()) return false;

    // unknown cancel
    if (b->cancel_order(12345)) return false;

    // two orders at the same price get added up
    b->add_order(1, Side::Buy, 99.50, 10);
    b->add_order(2, Side::Buy, 99.50, 15);
    b->add_order(3, Side::Buy, 99.00, 5);
    b->add_order(4, Side::Buy, 98.00, 7);
    b->add_order(5, Side::Sell, 100.01, 20);

    BBO x = b->get_bbo();
    if (!x.has_bid || x.bid_price != 99.50 || x.bid_qty != 25) return false;
    if (!x.has_ask || x.ask_price != 100.01 || x.ask_qty != 20) return false;

    // only 3 bid levels exist, asking for 10 should give 3
    vector<Level> bids = b->get_bids(10);
    if (bids.size() != 3) return false;
    if (bids[0].price != 99.50 || bids[1].price != 99.00 || bids[2].price != 98.00) return false;

    // cancel twice: second one must fail
    if (!b->cancel_order(1)) return false;
    if (b->cancel_order(1)) return false;
    if (b->get_bbo().bid_qty != 15) return false;

    // removing the whole best level moves best bid down
    b->cancel_order(2);
    if (b->get_bbo().bid_price != 99.00) return false;

    // removing the only ask empties that side
    b->cancel_order(5);
    if (b->get_bbo().has_ask) return false;

    return true;
}

int verify() {
    printf("Running correctness test...\n\n");

    bool edge_a = edge_tests<MapBook>();
    bool edge_b = edge_tests<VectorBook>();
    bool edge_c = edge_tests<ArrayBook>();

    // same random workload through all three, compare after every single operation
    vector<Op> ops = make_workload();
    unique_ptr<MapBook> a(new MapBook());
    unique_ptr<VectorBook> b(new VectorBook());
    unique_ptr<ArrayBook> c(new ArrayBook());

    bool b_ok = true, c_ok = true;
    long long checks = 0;

    for (int i = 0; i < (int)ops.size(); i++) {
        const Op &op = ops[i];
        if (op.type == ADD) {
            a->add_order(op.id, op.side, op.price, op.qty);
            b->add_order(op.id, op.side, op.price, op.qty);
            c->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == CANCEL) {
            bool ra = a->cancel_order(op.id);
            bool rb = b->cancel_order(op.id);
            bool rc = c->cancel_order(op.id);
            if (ra != rb) b_ok = false;
            if (ra != rc) c_ok = false;
        }

        BBO ba = a->get_bbo();
        vector<Level> bids_a = a->get_bids(10), asks_a = a->get_asks(10);

        if (!same_bbo(ba, b->get_bbo()) || !same_levels(bids_a, b->get_bids(10)) ||
            !same_levels(asks_a, b->get_asks(10)))
            b_ok = false;
        if (!same_bbo(ba, c->get_bbo()) || !same_levels(bids_a, c->get_bids(10)) ||
            !same_levels(asks_a, c->get_asks(10)))
            c_ok = false;
        checks++;

        if (!b_ok || !c_ok) {
            printf("mismatch at operation %d\n", i);
            break;
        }
    }

    // A has no other book to compare against, so it passes if it passes the edge tests
    // and B and C (built completely differently) agree with it on every operation
    bool pass_a = edge_a && b_ok && c_ok;
    bool pass_b = edge_b && b_ok;
    bool pass_c = edge_c && c_ok;

    printf("Edge case tests : A %s, B %s, C %s\n", edge_a ? "ok" : "FAIL",
           edge_b ? "ok" : "FAIL", edge_c ? "ok" : "FAIL");
    printf("Random workload : %d ops, BBO + 10 levels each side compared after every op (%lld checks)\n\n",
           NUM_OPS, checks);

    printf("Implementation A: %s\n", pass_a ? "PASS" : "FAIL");
    printf("Implementation B: %s\n", pass_b ? "PASS" : "FAIL");
    printf("Implementation C: %s\n", pass_c ? "PASS" : "FAIL");

    if (pass_a && pass_b && pass_c) {
        printf("\nAll implementations produce identical results.\n");
        return 0;
    }
    printf("\nImplementations do NOT agree.\n");
    return 1;
}

int main(int argc, char **argv) {
    string impl;
    int runs = 5;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0)
            return verify();
        if (strcmp(argv[i], "--impl") == 0 && i + 1 < argc)
            impl = argv[++i];
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
            runs = max(1, atoi(argv[++i]));
    }

    if (impl == "A")
        bench<MapBook>("A (std::map + unordered_map)", runs);
    else if (impl == "B")
        bench<VectorBook>("B (sorted vector + std::map ids)", runs);
    else if (impl == "C")
        bench<ArrayBook>("C (tick array + cached best + id vector)", runs);
    else if (impl == "none")
        printf("built %zu ops, no book run\n", make_workload().size()); // baseline for perf stat
    else {
        printf("usage: ./bench --impl A|B|C [--runs N]\n       ./bench --verify\n");
        return 1;
    }
    return 0;
}
